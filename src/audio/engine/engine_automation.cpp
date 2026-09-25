#include "engine_automation.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace blokkily::engine {
namespace {

// The value a lane leaves `tick` with (see AutomationLane::value_at), for a
// tick that need not be whole: a grid point rarely falls on one.
double lane_value(const std::vector<AutomationPoint>& points, double tick) {
    const auto after = std::upper_bound(points.begin(), points.end(), tick,
        [](double at, const AutomationPoint& point) { return at < static_cast<double>(point.at); });
    if (after == points.begin()) return points.front().value;
    const auto& before = *(after - 1);
    if (after == points.end() || static_cast<double>(before.at) == tick) return before.value;
    const double span = static_cast<double>(after->at - before.at);
    const double t = (tick - static_cast<double>(before.at)) / span;
    return before.value + (after->value - before.value) * t;
}

bool plays_lanes(AutomationMode mode) {
    return mode == AutomationMode::read || mode == AutomationMode::touch ||
           mode == AutomationMode::latch;
}

// The envelope at sample `sample`, between its two neighbouring grid points.
float envelope_at(const std::vector<float>& values, std::uint64_t sample) noexcept {
    const auto index = static_cast<std::size_t>(sample / automation_grid);
    if (index + 1 >= values.size()) return values.back();
    const auto offset = static_cast<float>(sample - index * automation_grid);
    const float a = values[index];
    const float b = values[index + 1];
    return a + (b - a) * (offset / static_cast<float>(automation_grid));
}

// Whether the live value of strip control `control` wins over its lane.
bool live_wins(const StripAutomation& automation, AutomationMode mode,
               std::size_t control) noexcept {
    switch (mode) {
    case AutomationMode::touch: return automation.touched[control];
    case AutomationMode::latch: return automation.touched[control] || automation.latched[control];
    case AutomationMode::write:
    case AutomationMode::off: return true;
    case AutomationMode::read: return false;
    }
    return false;
}

// One compiled parameter lane: an event at every point (a jump sends the
// value it leaves with) and at every grid step along a ramp.
ParameterLaneTimeline compile_parameter_lane(const AutomationLane& lane, AutomationMode mode,
                                             const TickClock& clock,
                                             std::uint64_t song_samples) {
    ParameterLaneTimeline compiled;
    compiled.parameter = lane.target.parameter_index;
    compiled.mode = mode;
    const auto last = song_samples == 0 ? 0 : song_samples - 1;
    const auto place = [&](double tick) {
        return std::min(sample_for_tick(clock, tick), last);
    };
    const auto add = [&compiled](std::uint64_t sample, double value) {
        if (!compiled.points.empty() && compiled.points.back().sample >= sample) {
            // A later point on the same sample is the one that stays.
            if (compiled.points.back().sample == sample) compiled.points.back().value = value;
            return;
        }
        compiled.points.push_back({sample, value});
    };
    const auto& points = lane.points;
    for (std::size_t i = 0; i < points.size();) {
        std::size_t group = i;
        while (group + 1 < points.size() && points[group + 1].at == points[i].at) ++group;
        const auto at = place(static_cast<double>(points[i].at));
        add(at, points[group].value);
        const std::size_t next = group + 1;
        if (next < points.size() && points[next].value != points[group].value) {
            const auto until = place(static_cast<double>(points[next].at));
            for (auto grid = (at / automation_grid + 1) * automation_grid; grid < until;
                 grid += automation_grid)
                add(grid, lane_value(points, clock.tick_at(static_cast<double>(grid))));
        }
        i = next;
    }
    return compiled;
}

ProcessorLanes& processor_entry(ArrangementAutomation& target, ProcessorAddress where) {
    for (auto& processor : target.processors)
        if (processor.where == where) return processor;
    target.processors.push_back({});
    target.processors.back().where = where;
    return target.processors.back();
}

} // namespace

void compile_automation(ArrangementAutomation& target, const Song& song, const TickClock& clock,
                        std::uint64_t song_samples) {
    const auto tracks = song.tracks.size();
    target.song_samples = song_samples;
    target.strips.assign(tracks, StripEnvelope{});
    target.instrument_lanes.assign(tracks, -1);
    target.processors.clear();
    target.any_strip = false;

    // Where every grid point of the song falls in ticks, worked out once for
    // every strip lane.
    std::vector<double> grid_ticks;
    const auto grid_ticks_for = [&]() -> const std::vector<double>& {
        if (grid_ticks.empty()) {
            const auto count = static_cast<std::size_t>(song_samples / automation_grid + 2);
            grid_ticks.resize(count);
            for (std::size_t i = 0; i < count; ++i)
                grid_ticks[i] = clock.tick_at(static_cast<double>(i * automation_grid));
        }
        return grid_ticks;
    };

    for (std::size_t owner = 0; owner < tracks; ++owner) {
        const auto mode = song.tracks[owner].automation_mode;
        if (!plays_lanes(mode)) continue;
        for (const auto& lane : song.tracks[owner].automation) {
            if (lane.points.empty()) continue;
            const auto& where = lane.target.processor;
            if (lane.target.kind == AutomationTarget::Kind::parameter) {
                auto& processor = processor_entry(target, where);
                processor.lanes.push_back(
                    compile_parameter_lane(lane, mode, clock, song_samples));
                continue;
            }
            // Strip lanes of return and master strips are not played (yet).
            if (where.kind != BusKind::track || where.bus >= tracks) continue;
            auto& strip = target.strips[where.bus];
            strip.mode = mode;
            const auto& ticks = grid_ticks_for();
            switch (lane.target.kind) {
            case AutomationTarget::Kind::gain:
                strip.gain.resize(ticks.size());
                for (std::size_t i = 0; i < ticks.size(); ++i)
                    strip.gain[i] =
                        static_cast<float>(db_to_linear(lane_value(lane.points, ticks[i])));
                break;
            case AutomationTarget::Kind::pan:
                strip.pan_left.resize(ticks.size());
                strip.pan_right.resize(ticks.size());
                for (std::size_t i = 0; i < ticks.size(); ++i) {
                    const double position = std::clamp(lane_value(lane.points, ticks[i]), -1.0, 1.0);
                    const double angle = (position + 1.0) * 0.25 * std::numbers::pi;
                    strip.pan_left[i] = static_cast<float>(std::cos(angle));
                    strip.pan_right[i] = static_cast<float>(std::sin(angle));
                }
                break;
            case AutomationTarget::Kind::mute:
                strip.unmuted.resize(ticks.size());
                for (std::size_t i = 0; i < ticks.size(); ++i)
                    strip.unmuted[i] = lane_value(lane.points, ticks[i]) >= 0.5 ? 0.0F : 1.0F;
                break;
            case AutomationTarget::Kind::parameter: break;
            }
            target.any_strip = true;
        }
    }

    // Each processor's lanes merged into one event list in sample order.
    for (auto& processor : target.processors) {
        std::vector<std::pair<TimedValue, std::uint32_t>> merged;
        for (std::uint32_t lane = 0; lane < processor.lanes.size(); ++lane)
            for (const auto& point : processor.lanes[lane].points) merged.push_back({point, lane});
        std::stable_sort(merged.begin(), merged.end(), [](const auto& a, const auto& b) {
            return a.first.sample < b.first.sample;
        });
        processor.event_values.clear();
        processor.event_lanes.clear();
        for (const auto& [value, lane] : merged) {
            processor.event_values.push_back(value);
            processor.event_lanes.push_back(lane);
        }
        processor.cursor = 0;
        processor.chase = true;
    }
    for (std::size_t index = 0; index < target.processors.size(); ++index) {
        const auto& where = target.processors[index].where;
        if (where.kind == BusKind::track && where.instrument() && where.bus < tracks)
            target.instrument_lanes[where.bus] = static_cast<std::int32_t>(index);
    }
}

StripRamp chunk_strip_gain(const StripAutomation& automation, const ArrangementAutomation& lanes,
                           std::size_t track, StripGain fixed, std::uint64_t song_position,
                           std::size_t frames, bool rolling) noexcept {
    StripRamp ramp;
    ramp.from = fixed;
    ramp.to = fixed;
    if (track >= lanes.strips.size() || !lanes.strips[track].active()) return ramp;
    const auto& envelope = lanes.strips[track];
    const bool live_gain = envelope.gain.empty() || live_wins(automation, envelope.mode, 0);
    const bool live_pan = envelope.pan_left.empty() || live_wins(automation, envelope.mode, 1);
    const bool live_mute = envelope.unmuted.empty() || live_wins(automation, envelope.mode, 2);
    const float fader = automation.fader.load(std::memory_order_relaxed);
    const float left = automation.pan_left.load(std::memory_order_relaxed);
    const float right = automation.pan_right.load(std::memory_order_relaxed);
    const float unmuted = automation.unmuted.load(std::memory_order_relaxed);
    const float solo = automation.solo_gate.load(std::memory_order_relaxed);

    struct Point {
        StripGain gain;
        float fader;
        float audible;
    };
    const auto at = [&](std::uint64_t sample) {
        const float gain = live_gain ? fader : envelope_at(envelope.gain, sample);
        const float heard = (live_mute ? unmuted : envelope_at(envelope.unmuted, sample)) * solo;
        const float pan_l = live_pan ? left : envelope_at(envelope.pan_left, sample);
        const float pan_r = live_pan ? right : envelope_at(envelope.pan_right, sample);
        const float level = gain * heard;
        return Point{{level * pan_l, level * pan_r}, level, heard};
    };
    const auto first = at(song_position);
    // Stopped, the song rests where it is: the envelope holds its value.
    const auto last = rolling ? at(song_position + frames) : first;
    ramp.from = first.gain;
    ramp.to = last.gain;
    ramp.fader_from = first.fader;
    ramp.fader_to = last.fader;
    ramp.audible_from = first.audible;
    ramp.audible_to = last.audible;
    ramp.automated = true;
    return ramp;
}

void apply_strip_touch(StripAutomation& automation, std::size_t control, bool touching,
                       bool rolling) noexcept {
    if (control >= strip_controls) return;
    automation.touched[control] = touching;
    if (touching && rolling) automation.latched[control] = true;
}

void release_automation(StripAutomation& automation) noexcept { automation.latched.fill(false); }

void release_parameter_holds(const ArrangementAutomation& lanes) noexcept {
    for (const auto& processor : lanes.processors)
        for (const auto& lane : processor.lanes) lane.held = false;
}

const ProcessorLanes* lanes_for(const ArrangementAutomation& lanes,
                                ProcessorAddress where) noexcept {
    for (const auto& processor : lanes.processors)
        if (processor.where == where) return &processor;
    return nullptr;
}

void seek_automation(const ArrangementAutomation& lanes, std::uint64_t position) noexcept {
    for (const auto& processor : lanes.processors) {
        const auto found = std::lower_bound(
            processor.event_values.begin(), processor.event_values.end(), position,
            [](const TimedValue& value, std::uint64_t sample) { return value.sample < sample; });
        processor.cursor = static_cast<std::size_t>(found - processor.event_values.begin());
        processor.chase = true;
    }
}

std::size_t automation_events(const ProcessorLanes& processor, std::uint64_t position,
                              std::uint64_t end, std::span<PluginEvent> out) noexcept {
    std::size_t count = 0;
    if (processor.chase) {
        processor.chase = false;
        for (const auto& lane : processor.lanes) {
            if (lane.held || lane.points.empty() || count >= out.size()) continue;
            const auto after = std::upper_bound(
                lane.points.begin(), lane.points.end(), position,
                [](std::uint64_t sample, const TimedValue& value) { return sample < value.sample; });
            const double value =
                after == lane.points.begin() ? lane.points.front().value : (after - 1)->value;
            out[count++] = {PluginEvent::Type::parameter_value, 0, lane.parameter, value};
        }
    }
    const auto& values = processor.event_values;
    while (processor.cursor < values.size() && values[processor.cursor].sample < end &&
           count < out.size()) {
        const auto& timed = values[processor.cursor];
        const auto& lane = processor.lanes[processor.event_lanes[processor.cursor]];
        if (!lane.held) {
            // An event that could not be delivered in its own chunk (the
            // budget was full) arrives at the start of the next.
            const auto offset = timed.sample > position ? timed.sample - position : 0;
            out[count++] = {PluginEvent::Type::parameter_value,
                            static_cast<std::uint32_t>(offset), lane.parameter, timed.value};
        }
        ++processor.cursor;
    }
    return count;
}

void note_parameter_edit(const ArrangementAutomation& lanes, ProcessorAddress where,
                         const ParameterEdit& edit) noexcept {
    const auto* processor = lanes_for(lanes, where);
    if (processor == nullptr) return;
    for (const auto& lane : processor->lanes) {
        if (lane.parameter != edit.parameter) continue;
        if (lane.mode == AutomationMode::touch)
            lane.held = edit.kind != ParameterEdit::Kind::end;
        else if (lane.mode == AutomationMode::latch)
            lane.held = true;
    }
}

float mix_into_ramp(StereoBlock bus, std::span<const float> track_left,
                    std::span<const float> track_right, const StripRamp& ramp) noexcept {
    const auto frames =
        std::min({bus.left.size(), bus.right.size(), track_left.size(), track_right.size()});
    if (frames == 0) return 0.0F;
    const float step = 1.0F / static_cast<float>(frames);
    const float left_step = (ramp.to.left - ramp.from.left) * step;
    const float right_step = (ramp.to.right - ramp.from.right) * step;
    float peak = 0.0F;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float at = static_cast<float>(frame);
        const float left = track_left[frame] * (ramp.from.left + left_step * at);
        const float right = track_right[frame] * (ramp.from.right + right_step * at);
        bus.left[frame] += left;
        bus.right[frame] += right;
        peak = std::max({peak, std::abs(left), std::abs(right)});
    }
    return peak;
}

} // namespace blokkily::engine
