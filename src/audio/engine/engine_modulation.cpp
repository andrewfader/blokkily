#include "engine_modulation.hpp"

#include <algorithm>
#include <cmath>

namespace blokkily::engine {

namespace {

// The tracks that must render before `track` does: the sources of its keys,
// of the followers that modulate its processors, and of the instrument
// output it plays.
std::vector<std::vector<std::uint32_t>> feeders(const Song& song) {
    std::vector<std::vector<std::uint32_t>> fed_by(song.tracks.size());
    const auto add = [&](std::size_t track, std::uint32_t source) {
        if (track >= fed_by.size() || source >= fed_by.size() || source == track) return;
        auto& list = fed_by[track];
        if (std::find(list.begin(), list.end(), source) == list.end()) list.push_back(source);
    };
    for (std::size_t t = 0; t < song.tracks.size(); ++t)
        for (const auto& slot : song.tracks[t].inserts)
            if (slot.sidechain) add(t, *slot.sidechain);
    for (std::size_t t = 0; t < song.tracks.size(); ++t)
        if (const auto& source = song.tracks[t].source) add(t, source->track);
    for (const auto& modulator : song.modulators) {
        if (modulator.kind != Modulator::Kind::follower) continue;
        for (const auto& target : modulator.targets)
            if (target.processor.kind == BusKind::track)
                add(target.processor.bus, modulator.source_track);
    }
    return fed_by;
}

// Every track once, each after what feeds it, otherwise in track order. A
// loop (which the schema refuses for keys, and which followers may make) is
// broken by taking the lowest track still waiting.
std::vector<std::uint32_t> render_order(const Song& song) {
    const auto fed_by = feeders(song);
    const auto count = fed_by.size();
    std::vector<bool> placed(count, false);
    std::vector<std::uint32_t> order;
    order.reserve(count);
    while (order.size() < count) {
        std::size_t next = count;
        for (std::size_t t = 0; t < count && next == count; ++t) {
            if (placed[t]) continue;
            const bool ready = std::all_of(fed_by[t].begin(), fed_by[t].end(),
                                           [&](std::uint32_t source) { return placed[source]; });
            if (ready) next = t;
        }
        if (next == count)
            for (std::size_t t = 0; t < count && next == count; ++t)
                if (!placed[t]) next = t;
        placed[next] = true;
        order.push_back(static_cast<std::uint32_t>(next));
    }
    return order;
}

} // namespace

void compile_routing(ArrangementRouting& target, const Song& song, const ParameterRange& range) {
    target.keys.clear();
    const auto keys = [&](BusKind kind, std::size_t bus, const std::vector<EffectSlot>& slots) {
        for (std::size_t slot = 0; slot < slots.size(); ++slot)
            if (slots[slot].sidechain && *slots[slot].sidechain < song.tracks.size())
                target.keys.push_back({{kind, static_cast<std::uint32_t>(bus),
                                        static_cast<std::int32_t>(slot)},
                                       *slots[slot].sidechain});
    };
    for (std::size_t t = 0; t < song.tracks.size(); ++t) keys(BusKind::track, t, song.tracks[t].inserts);
    for (std::size_t r = 0; r < song.returns.size(); ++r) keys(BusKind::ret, r, song.returns[r].inserts);
    keys(BusKind::master, 0, song.master_inserts);

    target.feeds.clear();
    for (std::size_t t = 0; t < song.tracks.size(); ++t)
        if (const auto& source = song.tracks[t].source; source && source->track < song.tracks.size())
            target.feeds.push_back({source->track, source->output, static_cast<std::uint32_t>(t)});

    target.modulators.clear();
    target.sinks.clear();
    target.shares.clear();
    const auto modulators = std::min(song.modulators.size(), maximum_modulators);
    // Grouped by parameter, so several modulators on one parameter add up
    // into the one event the plugin receives.
    struct Pending {
        ProcessorAddress where;
        std::int32_t parameter;
        ModulationShare share;
    };
    std::vector<Pending> pending;
    for (std::size_t m = 0; m < modulators; ++m) {
        const auto& modulator = song.modulators[m];
        target.modulators.push_back({modulator.kind, modulator.source_track});
        const auto targets = std::min(modulator.targets.size(), Modulator::maximum_targets);
        for (std::size_t t = 0; t < targets; ++t) {
            const auto& aim = modulator.targets[t];
            const auto span = range ? range(aim.processor, aim.parameter_index) : std::nullopt;
            if (!span || !std::isfinite(*span)) continue;
            pending.push_back({aim.processor, aim.parameter_index,
                               {static_cast<std::uint32_t>(m), static_cast<std::uint32_t>(t),
                                static_cast<float>(*span)}});
        }
    }
    for (std::size_t i = 0; i < pending.size(); ++i) {
        bool seen = false;
        for (std::size_t j = 0; j < i && !seen; ++j)
            seen = pending[j].where == pending[i].where &&
                   pending[j].parameter == pending[i].parameter;
        if (seen) continue;
        ModulationSink sink{pending[i].where, pending[i].parameter,
                            static_cast<std::uint32_t>(target.shares.size()), 0};
        for (std::size_t j = i; j < pending.size(); ++j)
            if (pending[j].where == sink.where && pending[j].parameter == sink.parameter) {
                target.shares.push_back(pending[j].share);
                ++sink.count;
            }
        target.sinks.push_back(sink);
    }
    target.order = render_order(song);
}

void apply_modulation_controls(ModulationPlayback& playback, const Song& song) noexcept {
    const auto count = std::min(song.modulators.size(), maximum_modulators);
    for (std::size_t m = 0; m < count; ++m) {
        const auto& modulator = song.modulators[m];
        auto& controls = playback.controls[m];
        controls.shape.store(static_cast<std::uint8_t>(modulator.shape), std::memory_order_relaxed);
        controls.rate_hz.store(static_cast<float>(modulator.rate_hz), std::memory_order_relaxed);
        controls.sync_beats.store(static_cast<float>(modulator.sync_beats),
                                  std::memory_order_relaxed);
        controls.value.store(static_cast<float>(std::clamp(modulator.value, 0.0, 1.0)),
                             std::memory_order_relaxed);
        controls.attack_ms.store(static_cast<float>(modulator.attack_ms), std::memory_order_relaxed);
        controls.release_ms.store(static_cast<float>(modulator.release_ms),
                                  std::memory_order_relaxed);
        const auto targets = std::min(modulator.targets.size(), Modulator::maximum_targets);
        for (std::size_t t = 0; t < targets; ++t)
            controls.depth[t].store(static_cast<float>(std::clamp(modulator.targets[t].depth, -1.0, 1.0)),
                                    std::memory_order_relaxed);
    }
}

void release_sinks(ModulationPlayback& playback, const ArrangementRouting& before,
                   const ArrangementRouting& after) noexcept {
    for (const auto& sink : before.sinks) {
        const bool kept = std::any_of(after.sinks.begin(), after.sinks.end(),
                                      [&](const ModulationSink& other) {
                                          return other.where == sink.where &&
                                                 other.parameter == sink.parameter;
                                      });
        if (kept) continue;
        for (auto& released : playback.released)
            if (!released.pending) {
                released = {sink.where, sink.parameter, true};
                break;
            }
    }
}

void advance_modulators(ModulationPlayback& playback, const ArrangementRouting& routing,
                        const ChunkTime& time) noexcept {
    const auto count = std::min(routing.modulators.size(), maximum_modulators);
    for (std::size_t m = 0; m < count; ++m) {
        auto& state = playback.state[m];
        const auto& controls = playback.controls[m];
        switch (routing.modulators[m].kind) {
        case Modulator::Kind::lfo: {
            const double rate = controls.rate_hz.load(std::memory_order_relaxed);
            const double sync = controls.sync_beats.load(std::memory_order_relaxed);
            double phase = state.free_phase;
            if (time.rolling) {
                phase = sync > 0.0 ? time.beats / sync : time.seconds * rate;
                state.free_phase = phase;
            }
            const auto shape = static_cast<LfoShape>(controls.shape.load(std::memory_order_relaxed));
            state.output = lfo_value(shape, phase);
            if (!time.rolling && time.sample_rate > 0.0) {
                const double cycles_per_second = sync > 0.0 ? time.bpm / 60.0 / sync : rate;
                state.free_phase = std::fmod(
                    phase + cycles_per_second * static_cast<double>(time.frames) / time.sample_rate,
                    1.0e6);
            }
            break;
        }
        case Modulator::Kind::macro:
            state.output = controls.value.load(std::memory_order_relaxed);
            break;
        case Modulator::Kind::follower:
            state.output = state.follower.level();
            break;
        }
    }
}

void follow_track(ModulationPlayback& playback, const ArrangementRouting& routing,
                  std::uint32_t track, StereoBlock audio, double sample_rate) noexcept {
    const auto count = std::min(routing.modulators.size(), maximum_modulators);
    for (std::size_t m = 0; m < count; ++m) {
        const auto& modulator = routing.modulators[m];
        if (modulator.kind != Modulator::Kind::follower || modulator.source_track != track)
            continue;
        auto& state = playback.state[m];
        const auto& controls = playback.controls[m];
        state.output = state.follower.follow(audio, sample_rate,
                                             controls.attack_ms.load(std::memory_order_relaxed),
                                             controls.release_ms.load(std::memory_order_relaxed));
    }
}

std::size_t modulation_events(ModulationPlayback& playback, const ArrangementRouting& routing,
                              ProcessorAddress where, std::span<PluginEvent> out) noexcept {
    std::size_t count = 0;
    for (auto& released : playback.released) {
        if (!released.pending || !(released.where == where) || count >= out.size()) continue;
        out[count++] = {PluginEvent::Type::parameter_modulation, 0, released.parameter, 0.0};
        released.pending = false;
    }
    for (const auto& sink : routing.sinks) {
        if (!(sink.where == where) || count >= out.size()) continue;
        double amount = 0.0;
        for (std::uint32_t i = 0; i < sink.count; ++i) {
            const auto& share = routing.shares[sink.first + i];
            if (share.modulator >= maximum_modulators ||
                share.target >= Modulator::maximum_targets)
                continue;
            const float depth =
                playback.controls[share.modulator].depth[share.target].load(std::memory_order_relaxed);
            amount += static_cast<double>(playback.state[share.modulator].output) * depth *
                      share.range;
        }
        out[count++] = {PluginEvent::Type::parameter_modulation, 0, sink.parameter, amount};
    }
    return count;
}

std::optional<std::uint32_t> sidechain_source(const ArrangementRouting& routing,
                                              ProcessorAddress where) noexcept {
    for (const auto& key : routing.keys)
        if (key.where == where) return key.source;
    return std::nullopt;
}

void reset_modulation(ModulationPlayback& playback) noexcept {
    for (auto& state : playback.state) {
        state.free_phase = 0.0;
        state.output = 0.0F;
        state.follower.reset();
    }
}

} // namespace blokkily::engine
