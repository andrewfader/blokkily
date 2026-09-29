#pragma once

// Modulators and sidechain keys in the engine (phase 2, waves 5.1 and 5.2).
// Private to SongEngine.
//
// What routes where is compiled from the song on the control thread with the
// rest of the arrangement (ArrangementRouting) and handed to the render
// callback in the same slot, through the same handoff word: the callback never
// sees a route list change under it. What a producer turns while the song
// plays - an LFO's rate or shape, a macro, a depth, a follower's times -
// reaches the callback as atomics (ModulationControls), the way a fader does,
// so turning a knob recompiles nothing and never lets go of a note.
//
// Per chunk (plan F-D): before any track renders, every LFO and macro is
// evaluated once, at the chunk's first sample. A follower reads its source
// track's buffer as soon as that track has rendered (after its inserts,
// before its fader), and the render order puts a follower's source before the
// tracks it modulates, so they hear this chunk's level; a track the order
// cannot put later (a loop, or the follower's own track) hears the level of
// the chunk before. Each processor is handed one parameter_modulation event
// per modulated parameter, at sample offset 0 ahead of everything else it
// plays in the chunk, carrying the sum of what every modulator on it adds.
// While the transport rolls an LFO's phase is taken from the song position,
// so a bounce modulates exactly as playback did.

#include "blokkily/model/modulation.hpp"
#include "blokkily/model/processor_address.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/model/timebase.hpp"
#include "blokkily/audio/modulation.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

namespace blokkily::engine {

// The parameter range of one processor's parameter, or nothing when the
// processor or the parameter does not exist. Control thread.
using ParameterRange = std::function<std::optional<double>(ProcessorAddress, std::int32_t)>;

struct CompiledModulator {
    Modulator::Kind kind = Modulator::Kind::lfo;
    std::uint32_t source_track = 0;
};

// One modulator's share of one parameter: which modulator, which of its
// targets (for its live depth), and the parameter's range.
struct ModulationShare {
    std::uint32_t modulator = 0;
    std::uint32_t target = 0;
    float range = 1.0F;
};

// One modulated parameter and the shares that add up on it.
struct ModulationSink {
    ProcessorAddress where{};
    std::int32_t parameter = 0;
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

struct SidechainKey {
    ProcessorAddress where{};
    std::uint32_t source = 0;
};

// An instrument output broken out to a track of its own (wave 5.2): output
// `output` of the instrument on `source` renders into `destination`'s buffer.
struct InstrumentFeed {
    std::uint32_t source = 0;
    std::uint32_t output = 1;
    std::uint32_t destination = 0;
};

// Compiled with the arrangement; read-only to the callback.
struct ArrangementRouting {
    std::vector<CompiledModulator> modulators;
    std::vector<ModulationSink> sinks;
    std::vector<ModulationShare> shares;
    std::vector<SidechainKey> keys;
    std::vector<InstrumentFeed> feeds;
    // Every track index once: sources of keys, followers and instrument
    // outputs before the tracks they feed, otherwise in track order.
    std::vector<std::uint32_t> order;
};

// A modulator's live controls. The control thread writes (apply_mix); the
// callback reads.
struct ModulatorControls {
    std::atomic<std::uint8_t> shape{0};
    std::atomic<float> rate_hz{1.0F};
    std::atomic<float> sync_beats{0.0F};
    std::atomic<float> value{0.0F};
    std::atomic<float> attack_ms{10.0F};
    std::atomic<float> release_ms{120.0F};
    std::array<std::atomic<float>, Modulator::maximum_targets> depth{};
};

// What the callback keeps between chunks for one modulator.
struct ModulatorState {
    double free_phase = 0.0;
    float output = 0.0F;
    EnvelopeFollower follower;
};

// A parameter whose modulation went away with a new arrangement: it is sent
// one last modulation of 0, so the plugin does not keep the offset.
struct ReleasedSink {
    ProcessorAddress where{};
    std::int32_t parameter = 0;
    bool pending = false;
};

struct ModulationPlayback {
    std::array<ModulatorControls, maximum_modulators> controls{};
    std::array<ModulatorState, maximum_modulators> state{};
    std::array<ReleasedSink, 64> released{};
};

// Control thread: compiles the song's keys, modulators and render order.
// Targets whose processor or parameter the engine does not have send
// nothing.
void compile_routing(ArrangementRouting& target, const Song& song, const ParameterRange& range);

// Control thread, any time: the live controls from the song.
void apply_modulation_controls(ModulationPlayback& playback, const Song& song) noexcept;

// Callback, as a new arrangement replaces `before`: sinks that are gone are
// owed a modulation of 0.
void release_sinks(ModulationPlayback& playback, const ArrangementRouting& before,
                   const ArrangementRouting& after) noexcept;

// Callback, before any track renders: every LFO and macro for this chunk.
// `rolling` takes the LFO phase from the song position (`seconds` and `beats`
// since the song's start); stopped, it runs on from where it was by `frames`
// at `bpm`.
struct ChunkTime {
    bool rolling = false;
    double seconds = 0.0;
    double beats = 0.0;
    double bpm = 120.0;
    double sample_rate = 48000.0;
    std::size_t frames = 0;
};
void advance_modulators(ModulationPlayback& playback, const ArrangementRouting& routing,
                        const ChunkTime& time) noexcept;

// Callback, straight after `track` rendered its pre-fader signal: every
// follower of that track takes its level.
void follow_track(ModulationPlayback& playback, const ArrangementRouting& routing,
                  std::uint32_t track, StereoBlock audio, double sample_rate) noexcept;

// Callback: the modulation events for the processor at `where`, written from
// the start of `out`, all at sample offset 0. Returns how many.
std::size_t modulation_events(ModulationPlayback& playback, const ArrangementRouting& routing,
                              ProcessorAddress where, std::span<PluginEvent> out) noexcept;

// The track keying the insert at `where`, if any.
[[nodiscard]] std::optional<std::uint32_t> sidechain_source(const ArrangementRouting& routing,
                                                            ProcessorAddress where) noexcept;

// While the callback is not running: every follower and free-running LFO
// back to rest, as a freshly prepared engine has them.
void reset_modulation(ModulationPlayback& playback) noexcept;

} // namespace blokkily::engine
