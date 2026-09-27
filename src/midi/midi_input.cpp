#include "blokkily/midi/midi_input.hpp"

#include <RtMidi.h>

#include <algorithm>
#include <bit>
#include <mutex>

namespace blokkily {

std::optional<MidiNote> decode_midi(std::span<const std::uint8_t> message) noexcept {
    if (message.size() < 3) return std::nullopt;
    const std::uint8_t status = message[0];
    const auto kind = static_cast<std::uint8_t>(status & 0xF0U);
    const auto channel = static_cast<std::uint8_t>(status & 0x0FU);
    const auto first = static_cast<std::uint8_t>(message[1] & 0x7FU);
    const auto second = static_cast<std::uint8_t>(message[2] & 0x7FU);
    switch (kind) {
    case 0x90:
        if (second == 0) return MidiNote{MidiNote::Kind::off, channel, first, 0};
        return MidiNote{MidiNote::Kind::on, channel, first, second};
    case 0x80: return MidiNote{MidiNote::Kind::off, channel, first, second};
    case 0xB0:
        // All Sound Off and All Notes Off: a panic button on the controller.
        if (first == 120 || first == 123) return MidiNote{MidiNote::Kind::all_off, channel, 0, 0};
        return std::nullopt;
    default: return std::nullopt;
    }
}

KeyMap identity_key_map() noexcept {
    KeyMap map{};
    for (std::size_t key = 0; key < map.size(); ++key)
        map[key] = {static_cast<std::int16_t>(key), 0.0F};
    return map;
}

struct MidiInput::Impl {
    Mode mode = Mode::device;
    std::unique_ptr<RtMidiIn> port;
    std::string port_name;
    std::atomic<bool> open{false};
    // Which tracks each channel plays: written by the control thread, read by
    // the port thread, never locked.
    std::array<std::atomic<TrackMask>, 16> routes;
    std::array<std::atomic<TunedKey>, 128> key_map;
    // Where each key of each channel went down, so its release reaches the
    // same tracks and the same pitch. Touched only by the one producer: the
    // port thread, or the caller of inject().
    struct Held {
        // Tracks the key sounds on: they were sent its note-on and not yet
        // its note-off.
        TrackMask sounding = 0;
        // Tracks owed a release the queue had no room for.
        TrackMask owed = 0;
        TunedKey pitch{};
        TunedKey owed_pitch{};
        bool sustained = false;
    };
    std::array<std::array<Held, 128>, 16> held{};
    std::array<std::atomic<bool>, 16> sustain_active{};
    // Whether any release is owed, so the next message need not look at every
    // key to find out. Written by the producer; read by release_pending().
    std::atomic<bool> any_owed{false};
    InputQueue queue;
    std::atomic<std::uint64_t> received{0};
    std::atomic<int> last_key{-1};
    std::atomic<int> last_velocity{0};
    // Guards opening and closing against each other, never the note path.
    std::mutex control;

    Impl() {
        const auto identity = identity_key_map();
        for (std::size_t key = 0; key < key_map.size(); ++key)
            key_map[key].store(identity[key], std::memory_order_relaxed);
        for (auto& route : routes) route.store(track_bit(0), std::memory_order_relaxed);
    }

    // Sends one event to every track of `tracks`, lowest first. Returns the
    // tracks the queue had room for.
    TrackMask push_to(TrackMask tracks, const PluginEvent& event) noexcept {
        TrackMask delivered = 0;
        for (TrackMask left = tracks; left != 0; left &= left - 1) {
            const auto track = static_cast<std::uint32_t>(std::countr_zero(left));
            if (!queue.push({track, event})) break;
            delivered |= track_bit(track);
        }
        return delivered;
    }

    // Sends what a key owes, keeping what still does not fit.
    void settle(Held& key) noexcept {
        if (key.owed == 0) return;
        key.owed &= ~push_to(key.owed, {PluginEvent::Type::note_off, 0, key.owed_pitch.key,
                                        0.0, key.owed_pitch.cents});
    }

    // Moves a key's sounding tracks to its owed ones and sends what fits.
    void release(Held& key) noexcept {
        if (key.sounding != 0) {
            key.owed |= key.sounding;
            key.owed_pitch = key.pitch;
            key.sounding = 0;
        }
        settle(key);
        if (key.owed != 0) any_owed.store(true, std::memory_order_release);
    }

    // Tries every owed release again, oldest message first in effect: a
    // release is always sent before anything that arrived after it.
    void retry_owed() noexcept {
        if (!any_owed.load(std::memory_order_acquire)) return;
        bool still = false;
        for (auto& channel : held)
            for (auto& key : channel) {
                settle(key);
                still = still || key.owed != 0;
            }
        any_owed.store(still, std::memory_order_release);
    }

    void release_everything() noexcept {
        for (auto& active : sustain_active) active.store(false, std::memory_order_relaxed);
        for (auto& channel : held)
            for (auto& key : channel) {
                key.sustained = false;
                release(key);
            }
    }
};

namespace {
void port_callback(double /*delta*/, std::vector<unsigned char>* message, void* user) {
    if (message == nullptr || user == nullptr) return;
    static_cast<MidiInput*>(user)->receive({message->data(), message->size()});
}

// RtMidi reports a missing sequencer or a vanished port through this rather
// than by throwing from its own thread; the caller learns of it from open().
void port_error(RtMidiError::Type /*type*/, const std::string& /*text*/, void* /*user*/) {}
} // namespace

MidiInput::MidiInput(Mode mode) : impl_(std::make_unique<Impl>()) { impl_->mode = mode; }

MidiInput::~MidiInput() { close(); }

MidiInput::Mode MidiInput::mode() const noexcept { return impl_->mode; }

std::vector<std::string> MidiInput::ports() {
    if (impl_->mode == Mode::deterministic) return {};
    std::vector<std::string> names;
    try {
        // A probe of its own, so listing never disturbs the port being played.
        RtMidiIn probe(RtMidi::UNSPECIFIED, "Blokkily probe");
        probe.setErrorCallback(port_error, nullptr);
        const unsigned int count = probe.getPortCount();
        for (unsigned int index = 0; index < count; ++index)
            names.push_back(probe.getPortName(index));
    } catch (const RtMidiError&) {
        return {};
    }
    // Our own ports are not a keyboard.
    std::erase_if(names, [](const std::string& name) {
        return name.find("Blokkily") != std::string::npos;
    });
    return names;
}

bool MidiInput::open(std::size_t index, std::string* error) {
    const auto fail = [error](const std::string& message) {
        if (error != nullptr) *error = message;
        return false;
    };
    close();
    std::lock_guard lock(impl_->control);
    if (impl_->mode == Mode::deterministic) {
        impl_->port_name = "Deterministic input";
        impl_->open.store(true, std::memory_order_release);
        return true;
    }
    try {
        auto port = std::make_unique<RtMidiIn>(RtMidi::UNSPECIFIED, "Blokkily");
        port->setErrorCallback(port_error, nullptr);
        // The ports list is filtered, so the index is found again by name.
        std::vector<std::string> offered;
        for (unsigned int candidate = 0; candidate < port->getPortCount(); ++candidate)
            offered.push_back(port->getPortName(candidate));
        std::vector<unsigned int> usable;
        for (unsigned int candidate = 0; candidate < offered.size(); ++candidate)
            if (offered[candidate].find("Blokkily") == std::string::npos)
                usable.push_back(candidate);
        if (index >= usable.size()) return fail("no MIDI input at that position");
        const auto chosen = usable[index];
        // SysEx, timing and active sensing are not notes; asking the backend to
        // drop them keeps a clocked controller from filling the callback.
        port->ignoreTypes(true, true, true);
        port->setCallback(port_callback, this);
        port->openPort(chosen, "Blokkily input");
        if (!port->isPortOpen()) return fail("the MIDI port refused to open");
        impl_->port_name = offered[chosen];
        impl_->port = std::move(port);
        impl_->open.store(true, std::memory_order_release);
        return true;
    } catch (const RtMidiError& failure) {
        return fail(failure.getMessage());
    }
}

bool MidiInput::open(const std::string& name, std::string* error) {
    if (impl_->mode == Mode::deterministic) return open(std::size_t{0}, error);
    const auto names = ports();
    const auto found = std::find_if(names.begin(), names.end(), [&name](const std::string& port) {
        return port.find(name) != std::string::npos;
    });
    if (found == names.end()) {
        if (error != nullptr) *error = "no MIDI input named " + name;
        return false;
    }
    return open(static_cast<std::size_t>(found - names.begin()), error);
}

void MidiInput::close() noexcept {
    std::lock_guard lock(impl_->control);
    impl_->open.store(false, std::memory_order_release);
    if (impl_->port) {
        try {
            impl_->port->cancelCallback();
            impl_->port->closePort();
        } catch (const RtMidiError&) {
        }
        impl_->port.reset();
    }
    impl_->port_name.clear();
    // The port thread is gone, so this thread may speak for it: whatever it
    // left held down is let go, or a keyboard unplugged mid-chord would leave
    // the chord ringing.
    impl_->retry_owed();
    impl_->release_everything();
}

bool MidiInput::is_open() const noexcept { return impl_->open.load(std::memory_order_acquire); }

std::string MidiInput::port_name() const {
    std::lock_guard lock(impl_->control);
    return impl_->port_name;
}

void MidiInput::set_routes(const ChannelRoutes& routes) noexcept {
    for (std::size_t channel = 0; channel < routes.size(); ++channel)
        impl_->routes[channel].store(routes[channel], std::memory_order_release);
}

ChannelRoutes MidiInput::routes() const noexcept {
    ChannelRoutes routes{};
    for (std::size_t channel = 0; channel < routes.size(); ++channel)
        routes[channel] = impl_->routes[channel].load(std::memory_order_acquire);
    return routes;
}

void MidiInput::set_track(std::size_t track) noexcept {
    ChannelRoutes routes{};
    routes.fill(track_bit(track));
    set_routes(routes);
}

std::size_t MidiInput::track() const noexcept {
    const auto mask = impl_->routes[0].load(std::memory_order_acquire);
    return mask == 0 ? 0 : static_cast<std::size_t>(std::countr_zero(mask));
}

bool MidiInput::release_pending() const noexcept {
    return impl_->any_owed.load(std::memory_order_acquire);
}

void MidiInput::set_key_map(const KeyMap& map) noexcept {
    for (std::size_t key = 0; key < map.size(); ++key)
        impl_->key_map[key].store(map[key], std::memory_order_release);
}

bool MidiInput::inject(std::span<const std::uint8_t> message) noexcept {
    if (impl_->mode != Mode::deterministic || !is_open()) return false;
    receive(message);
    return true;
}

void MidiInput::receive(std::span<const std::uint8_t> message) noexcept {
    if (!impl_->open.load(std::memory_order_acquire)) return;
    // Releases the queue had no room for go before anything newer.
    impl_->retry_owed();
    if (message.size() >= 2) {
        const std::uint8_t status = message[0];
        const auto kind = static_cast<std::uint8_t>(status & 0xF0U);
        const auto channel = static_cast<std::uint8_t>(status & 0x0FU);
        const auto first = static_cast<std::uint8_t>(message[1] & 0x7FU);
        const auto second = message.size() >= 3 ? static_cast<std::uint8_t>(message[2] & 0x7FU)
                                                : std::uint8_t{0};
        if (kind == 0xB0 && first == 64) {
            // Sustain pedal (CC 64): >= 64 is down, < 64 is up
            const bool down = second >= 64;
            impl_->sustain_active[channel].store(down, std::memory_order_release);
            if (!down) {
                auto& chan_held = impl_->held[channel];
                for (auto& k : chan_held) {
                    if (k.sustained && k.sounding != 0) {
                        impl_->release(k);
                        k.sustained = false;
                    }
                }
            }
            return;
        }
        // Continuous controllers (CC), Pitch Bend (0xE0), Channel Pressure (0xD0),
        // and Poly Pressure (0xA0):
        if (kind == 0xE0 || (kind == 0xB0 && first != 120 && first != 123) ||
            kind == 0xD0 || kind == 0xA0) {
            const std::uint32_t raw = static_cast<std::uint32_t>(status) |
                                      (static_cast<std::uint32_t>(first) << 8) |
                                      (static_cast<std::uint32_t>(second) << 16);
            const auto tracks = impl_->routes[channel].load(std::memory_order_acquire);
            (void)impl_->push_to(tracks, {PluginEvent::Type::midi_raw, 0,
                                          static_cast<std::int32_t>(raw), 0.0, 0.0});
            impl_->received.fetch_add(1, std::memory_order_relaxed);
            return;
        }
    }
    const auto note = decode_midi(message);
    if (!note) return;
    const auto ch = static_cast<std::uint8_t>(note->channel & 0x0FU);
    auto& channel = impl_->held[ch];
    if (note->kind == MidiNote::Kind::all_off) {
        impl_->sustain_active[ch].store(false, std::memory_order_release);
        for (auto& held : channel) {
            held.sustained = false;
            impl_->release(held);
        }
        return;
    }
    auto& held = channel[note->key];
    if (note->kind == MidiNote::Kind::off) {
        // If the sustain pedal is currently pressed on this channel, hold the
        // note sounding until the pedal is released.
        if (impl_->sustain_active[ch].load(std::memory_order_acquire) && held.sounding != 0) {
            held.sustained = true;
            return;
        }
        held.sustained = false;
        // The tracks it went down on, not the ones the routes name now.
        impl_->release(held);
        return;
    }
    // A key struck again without a release in between is let go first, so
    // the instrument never holds two voices for one key of the keyboard.
    if (held.sounding != 0) {
        held.sustained = false;
        impl_->release(held);
    }
    const auto pitch = impl_->key_map[note->key].load(std::memory_order_acquire);
    // A track still owed this key's release is not struck again: its release
    // would arrive after the new note and silence it.
    const auto tracks =
        impl_->routes[ch].load(std::memory_order_acquire) & ~held.owed;
    held.sounding = impl_->push_to(
        tracks, {PluginEvent::Type::note_on, 0, pitch.key, note->velocity / 127.0, pitch.cents});
    held.pitch = pitch;
    held.sustained = false;
    impl_->received.fetch_add(1, std::memory_order_relaxed);
    impl_->last_key.store(note->key, std::memory_order_relaxed);
    impl_->last_velocity.store(note->velocity, std::memory_order_relaxed);
}

bool MidiInput::is_sustain_active(std::uint8_t channel) const noexcept {
    return channel < impl_->sustain_active.size() &&
           impl_->sustain_active[channel].load(std::memory_order_acquire);
}

InputQueue& MidiInput::queue() noexcept { return impl_->queue; }

std::uint64_t MidiInput::notes_received() const noexcept {
    return impl_->received.load(std::memory_order_relaxed);
}

int MidiInput::last_key() const noexcept { return impl_->last_key.load(std::memory_order_relaxed); }

int MidiInput::last_velocity() const noexcept {
    return impl_->last_velocity.load(std::memory_order_relaxed);
}

} // namespace blokkily
