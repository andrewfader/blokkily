#pragma once

// Following an external MIDI clock (features/midi_clock_input.feature): what a
// drum machine or another sequencer sends - 24 clock ticks a beat, Start,
// Stop, Continue and the Song Position Pointer - turned into a tempo and
// transport moves for the song to follow. Pure: it is fed events with the
// time they arrived and says what they mean; nothing here touches an engine.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace blokkily {

struct ClockEvent {
    enum class Kind : std::uint8_t { tick, start, stop, resume, position };
    Kind kind = Kind::tick;
    std::uint64_t time_ns = 0;
    // A position: sixteenth notes from the start of the song (MIDI beats).
    std::uint16_t sixteenths = 0;
};

class ClockFollower {
public:
    // What a fed event asks of the transport.
    enum class Move : std::uint8_t { none, start, stop, resume, seek };

    // Takes one event, oldest first. A tick refines the tempo; the others
    // are returned as the move they ask for.
    Move feed(const ClockEvent& event) noexcept;

    // The tempo the ticks keep, once enough have arrived to say (a beat's
    // worth): the mean of the middle half of the last beat of tick
    // intervals, so jitter averages out and one late tick does not move it. Nothing before that, or after the clock stops
    // ticking for two seconds.
    [[nodiscard]] std::optional<double> bpm(std::uint64_t now_ns) const noexcept;
    // Where a Song Position Pointer put the song, in sixteenths.
    [[nodiscard]] std::uint16_t position() const noexcept { return position_; }
    [[nodiscard]] bool running() const noexcept { return running_; }
    void reset() noexcept;

private:
    static constexpr std::size_t window = 24; // a beat of ticks
    std::array<std::uint64_t, window> intervals_{};
    std::size_t count_ = 0;
    std::size_t next_ = 0;
    std::uint64_t last_tick_ = 0;
    bool running_ = false;
    std::uint16_t position_ = 0;
};

} // namespace blokkily
