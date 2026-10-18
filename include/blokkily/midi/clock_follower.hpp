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

// Keeps the song on the clock's beat, not only at its tempo
// (features/midi_clock_input.feature). Every tick says where the clock's song
// is - the beat it started or was pointed at, plus a 24th of a beat a tick -
// at the time it arrived. From the last eight beats of them it measures the
// clock's tempo by least squares, far steadier than a beat of intervals, and,
// told where the song was heard at each tick, it averages the difference
// over half a beat of ticks so jitter is not chased, then trims the tempo to
// close it over the next two beats, never by more than 2%. A song a little
// off the clock is then brought back, not left to drift for as long as it
// plays. Pure: the caller says where the song is.
class ClockLock {
public:
    // The beat the next tick marks (0 after Start, a pointer's position
    // after a seek), forgetting everything measured.
    void reset(double beat) noexcept;
    // The clock carries on after a pause: what was measured before the gap
    // is forgotten, the beat count is kept.
    void resume() noexcept;
    // A tick arrived at `time_ns`, when the song was heard at `song_beat`
    // (nothing when the song is not playing yet: the tick still counts).
    void tick(std::uint64_t time_ns, std::optional<double> song_beat) noexcept;
    // The song was moved by error() beats to meet the clock: the error starts
    // again from nothing.
    void aligned() noexcept;
    // The beat the next tick marks.
    [[nodiscard]] double next_beat() const noexcept { return beat_; }
    // The clock's tempo, once four beats of ticks say it.
    [[nodiscard]] std::optional<double> bpm() const noexcept;
    // How far the clock is ahead of the song, in beats, averaged.
    [[nodiscard]] double error() const noexcept { return error_; }
    [[nodiscard]] bool locked() const noexcept { return measured_ >= settle_ticks; }
    // The tempo the song should play at: the clock's (or `fallback` until it
    // is measured), trimmed to close the error.
    [[nodiscard]] double tempo(double fallback) const noexcept;

    static constexpr double horizon_beats = 2.0;
    static constexpr double largest_trim = 0.02;
    static constexpr std::size_t settle_ticks = 12;
    static constexpr std::size_t history = 8 * 24;

private:
    double beat_ = 0.0;
    double error_ = 0.0;
    std::size_t measured_ = 0;
    std::array<std::uint64_t, history> times_{};
    std::array<double, history> beats_{};
    std::size_t count_ = 0;
    std::size_t next_ = 0;
};

} // namespace blokkily
