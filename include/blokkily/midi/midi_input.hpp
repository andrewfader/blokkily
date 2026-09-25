#pragma once

#include "blokkily/audio/event_queue.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace blokkily {

// One MIDI message, reduced to what an instrument is told. Everything that is
// not a key going down or coming up — clock, controllers, SysEx — is not a
// note, and a take has nothing to write for it.
struct MidiNote {
    enum class Kind { on, off, all_off };
    Kind kind = Kind::on;
    std::uint8_t channel = 0;
    std::uint8_t key = 0;
    std::uint8_t velocity = 0;
};

// Decodes one complete message. A note-on at velocity zero is a note-off, as
// the MIDI specification says and as most keyboards send them; All Sound Off
// and All Notes Off let go of everything.
[[nodiscard]] std::optional<MidiNote> decode_midi(std::span<const std::uint8_t> message) noexcept;

// What a key of the controller sounds: the twelve-tone key the instrument is
// told plus the retune away from it. A controller's keys are the song's degrees
// in order, the way the on-screen piano's are, so a nineteen-tone song is
// played in nineteen tones from an ordinary keyboard.
struct TunedKey {
    std::int16_t key = 60;
    float cents = 0.0F;
};
using KeyMap = std::array<TunedKey, 128>;
// Every key sounds itself: the twelve equal semitones a MIDI keyboard assumes.
[[nodiscard]] KeyMap identity_key_map() noexcept;

// A MIDI input port. Messages arrive on the port's own thread and go straight
// to the render callback through a lock-free queue, so a note is not held up
// behind a busy interface. Only the key map and the target track are set from
// the control thread, and both are read without locking.
//
// A deterministic input has no port: inject() delivers bytes through the very
// same decode, map, and route path a port's callback runs, so verification
// drives what a keyboard would without needing one plugged in.
class MidiInput {
public:
    enum class Mode { device, deterministic };
    explicit MidiInput(Mode mode = Mode::device);
    ~MidiInput();
    MidiInput(const MidiInput&) = delete;
    MidiInput& operator=(const MidiInput&) = delete;

    [[nodiscard]] Mode mode() const noexcept;
    // The ports the system offers right now. Empty for a deterministic input,
    // and on a machine without a MIDI system.
    [[nodiscard]] std::vector<std::string> ports();
    // Opens the port at `index` of ports(), closing whatever was open. A
    // deterministic input opens a stand-in named `Deterministic input`.
    bool open(std::size_t index, std::string* error = nullptr);
    // Opens the first port whose name contains `name`.
    bool open(const std::string& name, std::string* error = nullptr);
    void close() noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] std::string port_name() const;

    // Which track a key pressed from now on sounds on. A key already held is
    // let go on the track it went down on, so changing track mid-phrase cannot
    // leave a voice sounding on the track that was left.
    void set_track(std::size_t track) noexcept;
    [[nodiscard]] std::size_t track() const noexcept;
    // How each key is tuned. Held keys keep the pitch they were struck at.
    void set_key_map(const KeyMap& map) noexcept;

    // Feeds a message through the port path. Only a deterministic input
    // accepts one: a device already has a producer, and the queue has room
    // for exactly one.
    bool inject(std::span<const std::uint8_t> message) noexcept;

    // The render callback's end of the input.
    [[nodiscard]] InputQueue& queue() noexcept;
    // How many keys have gone down since the port was opened, and the last of
    // them, for an activity light that says a keyboard is actually reaching
    // the application.
    [[nodiscard]] std::uint64_t notes_received() const noexcept;
    [[nodiscard]] int last_key() const noexcept;
    [[nodiscard]] int last_velocity() const noexcept;

    // Delivered by the port thread, or by inject(). Public only so the
    // backend callback can reach it.
    void receive(std::span<const std::uint8_t> message) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace blokkily
