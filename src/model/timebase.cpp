#include "blokkily/model/timebase.hpp"

#include "blokkily/model/song.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace blokkily {
namespace {

// Seconds spent crossing `ticks` ticks from a tempo of `bpm`, rising by
// `slope` bpm per tick. Closed form: the integral of 60 / (tpb * bpm(x)).
double segment_seconds(double ticks, double bpm, double slope, double ticks_per_beat) {
    if (ticks <= 0.0) return 0.0;
    if (slope == 0.0) return ticks * 60.0 / (bpm * ticks_per_beat);
    return 60.0 / (ticks_per_beat * slope) * std::log1p(slope * ticks / bpm);
}

// The inverse: ticks crossed in `seconds` from `bpm`, rising by `slope`.
double segment_ticks(double seconds, double bpm, double slope, double ticks_per_beat) {
    if (seconds <= 0.0) return 0.0;
    if (slope == 0.0) return seconds * bpm * ticks_per_beat / 60.0;
    return bpm / slope * std::expm1(seconds * ticks_per_beat * slope / 60.0);
}

// The slope of the tempo leaving point `index`: zero unless it ramps to a
// following point.
double slope_after(const std::vector<TempoPoint>& points, std::size_t index) {
    if (!points[index].ramp || index + 1 >= points.size()) return 0.0;
    const auto span = static_cast<double>(points[index + 1].at - points[index].at);
    if (span <= 0.0) return 0.0;
    return (points[index + 1].bpm - points[index].bpm) / span;
}

bool power_of_two_denominator(int denominator) {
    return denominator == 1 || denominator == 2 || denominator == 4 || denominator == 8 ||
           denominator == 16 || denominator == 32;
}

} // namespace

// ---- TempoMap ---------------------------------------------------------------

double TempoMap::bpm_at(Tick at) const {
    if (points.empty()) return TempoPoint{}.bpm;
    std::size_t index = 0;
    while (index + 1 < points.size() && points[index + 1].at <= at) ++index;
    const double slope = slope_after(points, index);
    const double into = static_cast<double>(std::max<Tick>(0, at - points[index].at));
    return points[index].bpm + slope * into;
}

double TempoMap::seconds_at(Tick at, Tick ticks_per_beat) const {
    return seconds_at_tick(static_cast<double>(at), ticks_per_beat);
}

double TempoMap::seconds_at_tick(double at, Tick ticks_per_beat) const {
    if (points.empty() || ticks_per_beat <= 0 || at <= 0.0) return 0.0;
    const auto tpb = static_cast<double>(ticks_per_beat);
    double seconds = 0.0;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const double from = static_cast<double>(points[index].at);
        const double to = index + 1 < points.size() ? static_cast<double>(points[index + 1].at)
                                                    : std::numeric_limits<double>::infinity();
        const double end = std::min(at, to);
        seconds += segment_seconds(end - from, points[index].bpm, slope_after(points, index), tpb);
        if (at <= to) break;
    }
    return seconds;
}

double TempoMap::tick_at_seconds(double seconds, Tick ticks_per_beat) const {
    if (points.empty() || ticks_per_beat <= 0 || !(seconds > 0.0)) return 0.0;
    const auto tpb = static_cast<double>(ticks_per_beat);
    double elapsed = 0.0;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const double from = static_cast<double>(points[index].at);
        const double slope = slope_after(points, index);
        if (index + 1 < points.size()) {
            const double span = static_cast<double>(points[index + 1].at) - from;
            const double length = segment_seconds(span, points[index].bpm, slope, tpb);
            if (seconds < elapsed + length)
                return from + segment_ticks(seconds - elapsed, points[index].bpm, slope, tpb);
            elapsed += length;
            continue;
        }
        return from + segment_ticks(seconds - elapsed, points[index].bpm, slope, tpb);
    }
    return 0.0;
}

bool TempoMap::valid() const {
    if (points.empty() || points.front().at != 0) return false;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto& point = points[index];
        if (!std::isfinite(point.bpm) || point.bpm < minimum_bpm || point.bpm > maximum_bpm)
            return false;
        if (index > 0 && point.at <= points[index - 1].at) return false;
    }
    return true;
}

void TempoMap::set(TempoPoint point) {
    const auto found = std::lower_bound(points.begin(), points.end(), point.at,
        [](const TempoPoint& existing, Tick at) { return existing.at < at; });
    if (found != points.end() && found->at == point.at) *found = point;
    else points.insert(found, point);
}

bool TempoMap::remove(Tick at) {
    if (at == 0) return false;
    const auto found = std::find_if(points.begin(), points.end(),
        [at](const TempoPoint& point) { return point.at == at; });
    if (found == points.end()) return false;
    points.erase(found);
    return true;
}

// ---- MeterMap ---------------------------------------------------------------

const MeterChange& MeterMap::meter_in(std::int32_t bar) const {
    static const MeterChange common_time{};
    if (changes.empty()) return common_time;
    std::size_t index = 0;
    while (index + 1 < changes.size() && changes[index + 1].bar <= bar) ++index;
    return changes[index];
}

Tick MeterMap::bar_length(std::int32_t bar) const {
    const auto& meter = meter_in(bar);
    if (meter.denominator <= 0) return whole_note_ticks;
    return static_cast<Tick>(meter.numerator) * whole_note_ticks / meter.denominator;
}

Tick MeterMap::bar_start(std::int32_t bar) const {
    if (bar <= 0 || changes.empty()) return 0;
    Tick start = 0;
    for (std::size_t index = 0; index < changes.size(); ++index) {
        const std::int32_t from = changes[index].bar;
        if (from >= bar) break;
        const std::int32_t to =
            index + 1 < changes.size() ? std::min(changes[index + 1].bar, bar) : bar;
        start += static_cast<Tick>(to - from) * bar_length(from);
    }
    return start;
}

std::int32_t MeterMap::bar_at(Tick at) const {
    if (at <= 0 || changes.empty()) return 0;
    for (std::size_t index = 0; index < changes.size(); ++index) {
        const std::int32_t from = changes[index].bar;
        const Tick start = bar_start(from);
        const Tick length = std::max<Tick>(1, bar_length(from));
        if (index + 1 < changes.size()) {
            const Tick next = bar_start(changes[index + 1].bar);
            if (at >= next) continue;
        }
        return from + static_cast<std::int32_t>((at - start) / length);
    }
    return 0;
}

Tick MeterMap::beat_length(Tick at) const {
    const auto& meter = meter_in(bar_at(at));
    if (meter.denominator <= 0) return whole_note_ticks / 4;
    return whole_note_ticks / meter.denominator;
}

MeterMap::Position MeterMap::position_at(Tick at) const {
    const Tick tick = std::max<Tick>(0, at);
    const auto bar = bar_at(tick);
    const Tick into = tick - bar_start(bar);
    const Tick beat = std::max<Tick>(1, beat_length(tick));
    constexpr Tick sixteenth = whole_note_ticks / 16;
    return {static_cast<int>(bar), static_cast<int>(into / beat),
            static_cast<int>((into % beat) / sixteenth)};
}

bool MeterMap::valid() const {
    if (changes.empty() || changes.front().bar != 0) return false;
    for (std::size_t index = 0; index < changes.size(); ++index) {
        const auto& change = changes[index];
        if (change.numerator < 1 || change.numerator > 64) return false;
        if (!power_of_two_denominator(change.denominator)) return false;
        if (index > 0 && change.bar <= changes[index - 1].bar) return false;
    }
    return true;
}

void MeterMap::set(MeterChange change) {
    const auto found = std::lower_bound(changes.begin(), changes.end(), change.bar,
        [](const MeterChange& existing, std::int32_t bar) { return existing.bar < bar; });
    if (found != changes.end() && found->bar == change.bar) *found = change;
    else changes.insert(found, change);
}

// ---- rebar --------------------------------------------------------------------

void rebar(Song& song, const MeterMap& before) {
    const MeterMap& after = song.meter;
    const auto moved = [&](Tick at) {
        if (at <= 0) return at;
        const auto bar = before.bar_at(at);
        const Tick offset = at - before.bar_start(bar);
        const Tick room = std::max<Tick>(1, after.bar_length(bar));
        return after.bar_start(bar) + std::min(offset, room - 1);
    };
    for (auto& clip : song.clips) clip.start = moved(clip.start);
    for (auto& clip : song.audio_clips) clip.start = moved(clip.start);
    std::vector<TempoPoint> points;
    points.reserve(song.tempo.points.size());
    for (auto point : song.tempo.points) {
        point.at = moved(point.at);
        if (!points.empty() && points.back().at == point.at) points.back() = point;
        else points.push_back(point);
    }
    song.tempo.points = std::move(points);
}

// ---- TickClock ----------------------------------------------------------------

TickClock::TickClock() : TickClock(TempoMap{}, 480, 48000.0) {}

TickClock::TickClock(const TempoMap& tempo, Tick ticks_per_beat, double sample_rate)
    : sample_rate_(sample_rate) {
    const double tpb = static_cast<double>(std::max<Tick>(1, ticks_per_beat));
    const auto& points = tempo.points.empty() ? TempoMap{}.points : tempo.points;
    segments_.reserve(points.size());
    double sample = 0.0;
    for (std::size_t index = 0; index < points.size(); ++index) {
        Segment segment;
        segment.tick = static_cast<double>(points[index].at);
        segment.sample = sample;
        segment.bpm = points[index].bpm;
        segment.slope = slope_after(points, index);
        segment.ticks_per_beat = tpb;
        // The same expression, in the same order, as the constant-tempo
        // engine used, so a song with one tempo lands on the same samples.
        segment.samples_per_tick = sample_rate * 60.0 / (segment.bpm * tpb);
        segments_.push_back(segment);
        if (index + 1 < points.size()) {
            const double span = static_cast<double>(points[index + 1].at) - segment.tick;
            sample += sample_rate *
                      segment_seconds(span, segment.bpm, segment.slope, tpb);
        }
    }
}

TickClock TickClock::uniform(double samples_per_tick, double sample_rate) {
    TickClock clock;
    clock.sample_rate_ = sample_rate;
    Segment segment;
    segment.samples_per_tick = samples_per_tick;
    segment.ticks_per_beat = 1.0;
    segment.bpm = samples_per_tick > 0.0 ? sample_rate * 60.0 / samples_per_tick : 0.0;
    clock.segments_.assign(1, segment);
    return clock;
}

const TickClock::Segment& TickClock::segment_for_tick(double at) const noexcept {
    std::size_t low = 0;
    std::size_t high = segments_.size();
    // The last segment whose first tick is at or before `at`.
    while (high - low > 1) {
        const std::size_t middle = low + (high - low) / 2;
        if (segments_[middle].tick <= at) low = middle;
        else high = middle;
    }
    return segments_[low];
}

const TickClock::Segment& TickClock::segment_for_sample(double sample) const noexcept {
    std::size_t low = 0;
    std::size_t high = segments_.size();
    while (high - low > 1) {
        const std::size_t middle = low + (high - low) / 2;
        if (segments_[middle].sample <= sample) low = middle;
        else high = middle;
    }
    return segments_[low];
}

double TickClock::sample_at(Tick at) const noexcept {
    return sample_at_tick(static_cast<double>(at));
}

double TickClock::sample_at_tick(double at) const noexcept {
    if (segments_.empty() || !(at > 0.0)) return 0.0;
    const auto& segment = segment_for_tick(at);
    const double into = at - segment.tick;
    if (segment.slope == 0.0) return segment.sample + into * segment.samples_per_tick;
    return segment.sample +
           sample_rate_ * segment_seconds(into, segment.bpm, segment.slope, segment.ticks_per_beat);
}

double TickClock::tick_at(double sample) const noexcept {
    if (segments_.empty() || !(sample > 0.0)) return 0.0;
    const auto& segment = segment_for_sample(sample);
    const double into = sample - segment.sample;
    if (segment.slope == 0.0) {
        if (!(segment.samples_per_tick > 0.0)) return segment.tick;
        return segment.tick + into / segment.samples_per_tick;
    }
    return segment.tick + segment_ticks(into / sample_rate_, segment.bpm, segment.slope,
                                        segment.ticks_per_beat);
}

double TickClock::bpm_at_sample(double sample) const noexcept {
    if (segments_.empty()) return TempoPoint{}.bpm;
    const auto& segment = segment_for_sample(std::max(0.0, sample));
    if (segment.slope == 0.0) return segment.bpm;
    return segment.bpm + segment.slope * (tick_at(sample) - segment.tick);
}

Tick tick_at_sample(const TickClock& clock, std::uint64_t sample) noexcept {
    // A hair of tolerance, so a sample that a tick was placed on exactly does
    // not read back as the tick before because of rounding in the division.
    const double tick = clock.tick_at(static_cast<double>(sample)) + 1e-6;
    if (!(tick < static_cast<double>(std::numeric_limits<Tick>::max()))) return 0;
    return static_cast<Tick>(std::floor(tick));
}

} // namespace blokkily
