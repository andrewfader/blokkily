#include "blokkily/midi/midi_output.hpp"

#include <RtMidi.h>

#include <algorithm>
#include <exception>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace blokkily {

// RtMidi's failures are caught as std::exception, never as RtMidiError: that
// class is defined wholly in its header, so each image carries its own type
// info for it, and on macOS a catch here does not match one thrown inside
// librtmidi. Uncaught, a sequencer that will not start aborts the program.

struct MidiOutput::Impl {
    Mode mode = Mode::device;
    std::atomic<bool> open{false};
    std::string port_name;
    Queue queue;
    // Guards opening and closing, the port, and send_now's list; never
    // touched by the render callback.
    std::mutex control;
    std::unique_ptr<RtMidiOut> port;
    std::vector<TimedMidi> now;      // send_now's, for the sender
    std::vector<TimedMidi> sent;     // a deterministic output's record
    // The sender thread (device mode): wakes often enough that a message is
    // sent within a fraction of a millisecond of when it is due.
    std::thread sender;
    std::atomic<bool> stopping{false};

    void deliver(const TimedMidi& message) {
        if (message.size == 0) return;
        if (mode == Mode::deterministic) {
            sent.push_back(message);
            return;
        }
        if (port == nullptr) return;
        try {
            port->sendMessage(message.bytes.data(), message.size);
        } catch (const std::exception&) {
        }
    }

    void run() {
        std::vector<TimedMidi> waiting;
        while (!stopping.load(std::memory_order_acquire)) {
            {
                std::lock_guard lock(control);
                TimedMidi popped;
                while (queue.pop(popped)) waiting.push_back(popped);
                // Messages due together keep the order they were queued in.
                std::stable_sort(waiting.begin(), waiting.end(),
                                 [](const TimedMidi& a, const TimedMidi& b) {
                                     return a.due_ns < b.due_ns;
                                 });
                const auto clock = monotonic_ns();
                std::size_t done = 0;
                while (done < waiting.size() && waiting[done].due_ns <= clock)
                    deliver(waiting[done++]);
                waiting.erase(waiting.begin(), waiting.begin() + static_cast<std::ptrdiff_t>(done));
                for (const auto& message : now) deliver(message);
                now.clear();
            }
            std::this_thread::sleep_for(std::chrono::microseconds(250));
        }
        // What was still waiting when the port closed is sent, not lost: a
        // note-off owed to a synthesizer must reach it.
        std::lock_guard lock(control);
        TimedMidi popped;
        while (queue.pop(popped)) waiting.push_back(popped);
        for (const auto& message : waiting) deliver(message);
        for (const auto& message : now) deliver(message);
        now.clear();
    }
};

namespace {
void port_error(RtMidiError::Type, const std::string&, void*) {}
} // namespace

MidiOutput::MidiOutput(Mode mode) : impl_(std::make_unique<Impl>()) { impl_->mode = mode; }

MidiOutput::~MidiOutput() { close(); }

MidiOutput::Mode MidiOutput::mode() const noexcept { return impl_->mode; }

std::vector<std::string> MidiOutput::ports() {
    if (impl_->mode == Mode::deterministic) return {"Deterministic output"};
    std::vector<std::string> names;
    try {
        RtMidiOut probe(RtMidi::UNSPECIFIED, "Blokkily probe");
        probe.setErrorCallback(port_error, nullptr);
        const unsigned int count = probe.getPortCount();
        for (unsigned int index = 0; index < count; ++index)
            names.push_back(probe.getPortName(index));
    } catch (const std::exception&) {
        return {};
    }
    // Our own input is not somewhere to send the song.
    std::erase_if(names, [](const std::string& name) {
        return name.find("Blokkily") != std::string::npos;
    });
    return names;
}

bool MidiOutput::open(const std::string& name, std::string* error) {
    const auto fail = [error](const std::string& message) {
        if (error != nullptr) *error = message;
        return false;
    };
    close();
    if (impl_->mode == Mode::deterministic) {
        std::lock_guard lock(impl_->control);
        impl_->port_name = "Deterministic output";
        impl_->open.store(true, std::memory_order_release);
        return true;
    }
    try {
        auto port = std::make_unique<RtMidiOut>(RtMidi::UNSPECIFIED, "Blokkily");
        port->setErrorCallback(port_error, nullptr);
        unsigned int chosen = port->getPortCount();
        for (unsigned int candidate = 0; candidate < port->getPortCount(); ++candidate)
            if (port->getPortName(candidate) == name) chosen = candidate;
        if (chosen == port->getPortCount()) return fail("no MIDI output named " + name);
        port->openPort(chosen, "Blokkily output");
        if (!port->isPortOpen()) return fail("the MIDI output refused to open");
        {
            std::lock_guard lock(impl_->control);
            impl_->port = std::move(port);
            impl_->port_name = name;
        }
        impl_->stopping.store(false, std::memory_order_release);
        impl_->sender = std::thread([impl = impl_.get()] { impl->run(); });
        impl_->open.store(true, std::memory_order_release);
        return true;
    } catch (const std::exception& failure) {
        return fail(failure.what());
    }
}

void MidiOutput::close() noexcept {
    impl_->open.store(false, std::memory_order_release);
    if (impl_->sender.joinable()) {
        impl_->stopping.store(true, std::memory_order_release);
        impl_->sender.join();
    }
    std::lock_guard lock(impl_->control);
    if (impl_->port != nullptr) {
        try {
            impl_->port->closePort();
        } catch (const std::exception&) {
        }
        impl_->port.reset();
    }
    impl_->port_name.clear();
}

bool MidiOutput::is_open() const noexcept { return impl_->open.load(std::memory_order_acquire); }

std::string MidiOutput::port_name() const {
    std::lock_guard lock(impl_->control);
    return impl_->port_name;
}

bool MidiOutput::send(const TimedMidi& message) noexcept {
    if (!is_open()) return false;
    return impl_->queue.push(message);
}

void MidiOutput::send_now(const TimedMidi& message) {
    if (!is_open()) return;
    std::lock_guard lock(impl_->control);
    if (impl_->mode == Mode::deterministic) {
        // In order: what the render callback queued first.
        TimedMidi popped;
        while (impl_->queue.pop(popped)) impl_->sent.push_back(popped);
        impl_->sent.push_back(message);
        return;
    }
    impl_->now.push_back(message);
}

std::vector<TimedMidi> MidiOutput::take_sent() {
    std::lock_guard lock(impl_->control);
    TimedMidi popped;
    while (impl_->queue.pop(popped)) impl_->sent.push_back(popped);
    std::vector<TimedMidi> taken;
    taken.swap(impl_->sent);
    return taken;
}

} // namespace blokkily
