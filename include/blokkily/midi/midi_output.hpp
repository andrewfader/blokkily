#pragma once

#include "blokkily/audio/event_queue.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace blokkily {

// One MIDI message on its way out: up to three bytes, the time it is due on
// monotonic_ns()'s clock (0: at once), and the engine frame it was played
// at (SongEngine counts every frame it renders), by which a test reads where
// in the song it was sent.
struct TimedMidi {
    std::uint64_t due_ns = 0;
    std::uint64_t frame = 0;
    std::array<std::uint8_t, 3> bytes{};
    std::uint8_t size = 0;
};

// A MIDI output port. The render callback hands it messages through a
// lock-free queue without waiting; a thread of the port's own sends each when
// it is due, so what the song plays reaches outboard gear at the time it is
// heard. The render callback is the queue's one producer; anything sent from
// another thread (the panic of an instrument going away) goes by send_now().
//
// A deterministic output has no port: what it is sent is kept, in order, for
// take_sent(), so verification reads what a synthesizer would have received
// without one plugged in.
class MidiOutput {
public:
    enum class Mode { device, deterministic };
    explicit MidiOutput(Mode mode = Mode::device);
    ~MidiOutput();
    MidiOutput(const MidiOutput&) = delete;
    MidiOutput& operator=(const MidiOutput&) = delete;

    [[nodiscard]] Mode mode() const noexcept;
    // The ports the system offers right now; a deterministic output offers
    // one, "Deterministic output".
    [[nodiscard]] std::vector<std::string> ports();
    // Opens the port named `name`, closing whatever was open.
    bool open(const std::string& name, std::string* error = nullptr);
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] std::string port_name() const;

    // Render callback: queues `message`. Never allocates, locks or blocks; a
    // full queue or a closed port refuses it.
    bool send(const TimedMidi& message) noexcept;
    // Any other thread: sends `message` now, after what is already queued.
    void send_now(const TimedMidi& message);
    // A deterministic output: everything sent since the last call, in order.
    [[nodiscard]] std::vector<TimedMidi> take_sent();

    using Queue = SpscQueue<TimedMidi, 4096>;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace blokkily
