#include "blokkily/sequencer/automation_take.hpp"

#include <algorithm>
#include <cmath>

namespace blokkily {
namespace {

bool same_control(const AutomationTarget& a, const AutomationTarget& b) {
    if (a.kind != b.kind || a.processor != b.processor) return false;
    return a.kind != AutomationTarget::Kind::parameter || a.parameter_index == b.parameter_index;
}

} // namespace

double AutomationTake::tolerance(const AutomationTarget& target) noexcept {
    switch (target.kind) {
    case AutomationTarget::Kind::gain: return 0.05;    // dB
    case AutomationTarget::Kind::pan: return 0.002;
    case AutomationTarget::Kind::mute: return 0.0;
    case AutomationTarget::Kind::parameter: return 1e-5;
    }
    return 0.0;
}

AutomationTake::Pass* AutomationTake::find_open(std::size_t track,
                                                const AutomationTarget& target) {
    for (auto& pass : passes_)
        if (pass.open && pass.track == track && same_control(pass.target, target)) return &pass;
    return nullptr;
}

bool AutomationTake::open(std::size_t track, const AutomationTarget& target) const {
    return std::any_of(passes_.begin(), passes_.end(), [&](const Pass& pass) {
        return pass.open && pass.track == track && same_control(pass.target, target);
    });
}

bool AutomationTake::ready() const noexcept {
    return std::any_of(passes_.begin(), passes_.end(), [](const Pass& pass) { return !pass.open; });
}

void AutomationTake::touch(std::size_t track, const AutomationTarget& target, Tick at,
                           double before) {
    if (find_open(track, target) != nullptr) return;
    Pass pass;
    pass.track = track;
    pass.target = target;
    pass.from = std::max<Tick>(0, at);
    pass.to = pass.from;
    pass.before = before;
    pass.points.push_back({pass.from, before});
    passes_.push_back(std::move(pass));
}

void AutomationTake::wrap(Pass& pass) {
    const double last = pass.points.back().value;
    Pass rest;
    rest.track = pass.track;
    rest.target = pass.target;
    rest.before = pass.before;
    rest.points.push_back({0, last});
    rest.changed = pass.changed;
    const Tick end = std::max(pass.points.back().at, length_);
    pass.to = end;
    pass.open = false;
    passes_.push_back(std::move(rest));
}

void AutomationTake::value(std::size_t track, const AutomationTarget& target, Tick at,
                           double value) {
    if (!std::isfinite(value)) return;
    at = std::max<Tick>(0, at);
    if (find_open(track, target) == nullptr) touch(track, target, at, value);
    auto* pass = find_open(track, target);
    // The song looped since the last move: the pass so far ends at the loop
    // point and a new one runs on from the top.
    if (at < pass->points.back().at) {
        wrap(*pass);
        pass = find_open(track, target);
    }
    if (value != pass->before) pass->changed = true;
    // A control held still and then moved was heard as a step: the value
    // before it holds up to the move. A control moving continuously (its
    // moves closer together than `step_gap`) is a ramp through its values,
    // which thinning can reduce to the points it needs.
    const auto& last = pass->points.back();
    if (at - last.at > step_gap && last.value != value) pass->points.push_back({at, last.value});
    pass->points.push_back({at, value});
    pass->to = at;
}

void AutomationTake::record(const Song& song, const StripMoveEvent& played) {
    const auto track = static_cast<std::size_t>(played.move.track);
    if (track >= song.tracks.size()) return;
    const auto mode = song.tracks[track].automation_mode;
    if (mode != AutomationMode::touch && mode != AutomationMode::latch &&
        mode != AutomationMode::write)
        return;
    AutomationTarget target;
    target.kind = played.move.control == StripControl::gain  ? AutomationTarget::Kind::gain
                  : played.move.control == StripControl::pan ? AutomationTarget::Kind::pan
                                                             : AutomationTarget::Kind::mute;
    target.processor = track_instrument(static_cast<std::uint32_t>(track));
    if (played.move.touching) {
        // The first move of a hold carries the value the control had.
        touch(track, target, played.tick, played.move.value);
        value(track, target, played.tick, played.move.value);
        return;
    }
    value(track, target, played.tick, played.move.value);
    // Touch hands the strip back to its lane on release; latch and write
    // keep the pass open until the transport stops.
    if (mode == AutomationMode::touch) release(track, target, played.tick);
}

bool AutomationTake::knows(ProcessorAddress where, std::int32_t parameter) const {
    return std::any_of(known_.begin(), known_.end(), [&](const KnownValue& known) {
        return known.where == where && known.parameter == parameter;
    });
}

void AutomationTake::record(const Song& song, const PluginEditEvent& event, Tick tick,
                            double fallback) {
    const auto& edit = event.edit;
    auto known = std::find_if(known_.begin(), known_.end(), [&](const KnownValue& entry) {
        return entry.where == event.where && entry.parameter == edit.parameter;
    });
    const double before = known != known_.end() ? known->value : fallback;
    if (edit.kind == ParameterEdit::Kind::value) {
        if (known == known_.end())
            known_.push_back({event.where, edit.parameter, edit.value});
        else
            known->value = edit.value;
    }
    if (!event.rolling) return;

    AutomationTarget target;
    target.kind = AutomationTarget::Kind::parameter;
    target.parameter_index = edit.parameter;
    target.processor = event.where;
    std::size_t owner = song.tracks.size();
    if (event.where.kind == BusKind::track) {
        owner = event.where.bus;
    } else {
        for (std::size_t track = 0; track < song.tracks.size() && owner == song.tracks.size();
             ++track)
            for (const auto& lane : song.tracks[track].automation)
                if (same_control(lane.target, target)) owner = track;
    }
    if (owner >= song.tracks.size()) return;
    const auto mode = song.tracks[owner].automation_mode;
    if (mode != AutomationMode::touch && mode != AutomationMode::latch &&
        mode != AutomationMode::write)
        return;
    switch (edit.kind) {
    case ParameterEdit::Kind::begin:
        touch(owner, target, tick, before);
        break;
    case ParameterEdit::Kind::value:
        touch(owner, target, tick, before);
        value(owner, target, tick, edit.value);
        break;
    case ParameterEdit::Kind::end:
        if (mode == AutomationMode::touch) release(owner, target, tick);
        break;
    }
}

void AutomationTake::close(Pass& pass, Tick at) {
    at = std::max<Tick>(0, at);
    if (at < pass.points.back().at) {
        wrap(pass);
        auto* rest = find_open(pass.track, pass.target);
        if (rest == nullptr) return;
        rest->to = at;
        rest->open = false;
        return;
    }
    pass.to = at;
    pass.open = false;
}

void AutomationTake::release(std::size_t track, const AutomationTarget& target, Tick at) {
    if (auto* pass = find_open(track, target)) close(*pass, at);
}

void AutomationTake::finish(Tick at) {
    // Closing can add a pass (a wrap), so the open ones are found by index.
    for (std::size_t index = 0; index < passes_.size(); ++index)
        if (passes_[index].open) close(passes_[index], at);
}

std::size_t AutomationTake::commit(Song& song) {
    std::size_t written = 0;
    std::vector<Pass> kept;
    for (auto& pass : passes_) {
        if (pass.open) {
            kept.push_back(std::move(pass));
            continue;
        }
        if (pass.track >= song.tracks.size()) continue;
        auto& lanes = song.tracks[pass.track].automation;
        auto lane = std::find_if(lanes.begin(), lanes.end(), [&](const AutomationLane& existing) {
            return same_control(existing.target, pass.target);
        });
        if (lane == lanes.end()) {
            // A control nobody moved makes no lane of its own.
            if (!pass.changed) continue;
            AutomationLane made;
            made.target = pass.target;
            // Before the pass the song sounded as the control was.
            made.points.push_back({0, pass.before});
            lanes.push_back(std::move(made));
            lane = lanes.end() - 1;
        }
        std::vector<AutomationPoint> points;
        for (const auto& point : pass.points)
            if (point.at >= pass.from && point.at <= pass.to) points.push_back(point);
        if (points.empty()) continue;
        if (!lane->write_pass(pass.from, pass.to, points)) continue;
        lane->thin(tolerance(pass.target));
        ++written;
    }
    passes_ = std::move(kept);
    return written;
}

} // namespace blokkily
