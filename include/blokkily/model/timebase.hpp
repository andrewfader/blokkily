#pragma once

// The song's timebase (plan F-A): where each tick falls in time, and how ticks
// group into bars. Every conversion between ticks, seconds and samples goes
// through here, so the engine, the transport, recording and the editors can
// never disagree about when a tick sounds or which bar it is in.

#include "blokkily/model/event.hpp"

#include <cstdint>
#include <vector>

namespace blokkily {

struct Song;

// One tempo on the song's tempo lane. From `at` the song plays at `bpm`; with
// `ramp`, the tempo moves linearly (in ticks) from `bpm` to the next point's
// bpm and arrives there exactly. A ramp on the last point has nowhere to go
// and holds its tempo.
struct TempoPoint {
    Tick at = 0;
    double bpm = 120.0;
    bool ramp = false;

    friend bool operator==(const TempoPoint&, const TempoPoint&) = default;
};

inline constexpr double minimum_bpm = 20.0;
inline constexpr double maximum_bpm = 300.0;

struct TempoMap {
    // Sorted by tick, unique ticks, points[0].at == 0, every bpm in
    // [minimum_bpm, maximum_bpm].
    std::vector<TempoPoint> points{TempoPoint{}};

    // The tempo at `at` (the one leaving it, at a point).
    [[nodiscard]] double bpm_at(Tick at) const;
    // Seconds from the start of the song to `at`, in closed form across ramps.
    // Ticks before 0 count as 0.
    [[nodiscard]] double seconds_at(Tick at, Tick ticks_per_beat) const;
    // The same with a fractional tick.
    [[nodiscard]] double seconds_at_tick(double at, Tick ticks_per_beat) const;
    // The inverse of seconds_at: the (fractional) tick `seconds` into the song.
    [[nodiscard]] double tick_at_seconds(double seconds, Tick ticks_per_beat) const;
    [[nodiscard]] bool valid() const;
    // Adds `point`, or replaces the point already at its tick.
    void set(TempoPoint point);
    // Removes the point at `at`. The point at tick 0 cannot be removed, and a
    // tick with no point changes nothing; both return false.
    bool remove(Tick at);

    friend bool operator==(const TempoMap&, const TempoMap&) = default;
};

// A time signature taking effect at the start of `bar` (0-based).
struct MeterChange {
    std::int32_t bar = 0;
    std::int16_t numerator = 4;
    std::int16_t denominator = 4;

    friend bool operator==(const MeterChange&, const MeterChange&) = default;
};

// Ticks in a whole note. Bars are measured at the song's 480 ticks a quarter:
// a bar of num/den lasts num * whole_note_ticks / den ticks.
inline constexpr Tick whole_note_ticks = 1920;

struct MeterMap {
    // Sorted by bar, unique bars, changes[0].bar == 0, numerator 1..64,
    // denominator a power of two from 1 to 32.
    std::vector<MeterChange> changes{MeterChange{}};

    // The change in effect in `bar` (the first one for a negative bar).
    [[nodiscard]] const MeterChange& meter_in(std::int32_t bar) const;
    [[nodiscard]] Tick bar_length(std::int32_t bar) const;   // num * 1920 / den
    [[nodiscard]] Tick bar_start(std::int32_t bar) const;    // 0 for bar <= 0
    [[nodiscard]] std::int32_t bar_at(Tick at) const;        // 0 for at <= 0
    // One beat of the meter in effect at `at`: a quarter in 4/4, an eighth
    // in 7/8.
    [[nodiscard]] Tick beat_length(Tick at) const;
    // 0-based bar, beat in the bar, and sixteenth in the beat.
    struct Position {
        int bar = 0;
        int beat = 0;
        int sixteenth = 0;

        friend bool operator==(const Position&, const Position&) = default;
    };
    [[nodiscard]] Position position_at(Tick at) const;
    [[nodiscard]] bool valid() const;
    // Makes `change` take effect at its bar, replacing a change already there.
    void set(MeterChange change);

    friend bool operator==(const MeterMap&, const MeterMap&) = default;
};

// After the meter map changed from `before` to song.meter, moves every clip,
// audio clip and tempo point so it keeps its bar number and its offset into
// that bar (decision 9). An offset that no longer fits its bar is clamped to
// the bar's last tick. Tempo points that land on the same tick keep the later
// one. Automation lanes are not moved.
void rebar(Song& song, const MeterMap& before);

// Ticks to samples and back, for one tempo map at one sample rate. Immutable
// and built on the control thread; every const member is safe to call from
// the render callback (no allocation, no locks).
class TickClock {
public:
    TickClock();   // 120 bpm, 480 ticks a beat, 48 kHz
    TickClock(const TempoMap& tempo, Tick ticks_per_beat, double sample_rate);
    // A clock with a constant number of samples per tick, for pattern preview,
    // which has no tempo map.
    [[nodiscard]] static TickClock uniform(double samples_per_tick, double sample_rate);

    // Where tick `at` falls, in (fractional) samples. Ticks before 0 are 0.
    [[nodiscard]] double sample_at(Tick at) const noexcept;
    [[nodiscard]] double sample_at_tick(double at) const noexcept;
    // The (fractional) tick at `sample`. Samples before 0 are tick 0.
    [[nodiscard]] double tick_at(double sample) const noexcept;
    [[nodiscard]] double sample_rate() const noexcept { return sample_rate_; }
    [[nodiscard]] double bpm_at_sample(double sample) const noexcept;

    friend bool operator==(const TickClock&, const TickClock&) = default;

private:
    // One stretch of constant or linearly ramping tempo.
    struct Segment {
        double tick = 0.0;         // first tick
        double sample = 0.0;       // where that tick falls
        double samples_per_tick = 0.0; // at the first tick
        // Change of bpm per tick over a ramp, and the bpm at the first tick;
        // slope 0 means constant.
        double bpm = 0.0;
        double slope = 0.0;
        double ticks_per_beat = 0.0;

        friend bool operator==(const Segment&, const Segment&) = default;
    };
    [[nodiscard]] const Segment& segment_for_tick(double at) const noexcept;
    [[nodiscard]] const Segment& segment_for_sample(double sample) const noexcept;

    std::vector<Segment> segments_;
    double sample_rate_ = 48000.0;
};

// The one sample-to-tick path: the tick sounding at `sample`, rounded down.
[[nodiscard]] Tick tick_at_sample(const TickClock& clock, std::uint64_t sample) noexcept;

} // namespace blokkily
