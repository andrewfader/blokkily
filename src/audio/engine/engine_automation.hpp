#pragma once

// Automation (item 3.1; decisions 3 and 11). Two kinds of lane reach the
// render callback, both compiled on the control thread with the arrangement:
//
// * Strip lanes (gain, pan, mute of a track) become a strip envelope: the
//   linear gain, the pan coefficients and the mute gate sampled every
//   `automation_grid` samples of the song. Every pow, cos and sin is taken
//   here, at compile time; the callback only interpolates between two
//   neighbouring grid points, and cuts its chunks at grid points so that the
//   ramp it applies across a chunk is exactly the envelope's (chunk stage 9).
// * Parameter lanes (a parameter of a track's instrument, or of an insert on
//   a track, a return or the master) become timeline events: a parameter_value
//   at every point and every grid step along a ramp, delivered to the
//   processor with the sample offset it falls on. After a seek, a loop wrap or
//   a new arrangement every lane is chased: its value at the new position is
//   sent first, so a parameter never keeps the value of where the song was.
//
// A track's automation mode says who wins (decision 3). Off and write compile
// no lanes for the track. Read plays them. Touch lets a control held by the
// producer override its lane while it is held; latch keeps overriding from
// the first touch until the transport stops; write overrides every strip
// control for as long as the transport runs. Strip controls are overridden by
// touch bits the callback sets from SongEngine::move(); plugin parameters by
// the plugin's own gesture, seen as its edits are drained. Either way the
// callback then plays the live value, which the control thread records.
//
// Solo is applied through each track's solo gate, a live value multiplied into
// the envelope, so that soloing another track silences an automated one
// without recompiling anything.

#include "blokkily/audio/event_timeline.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/model/processor_address.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/model/timebase.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace blokkily::engine {

// How far apart the strip envelope's precomputed points are, in samples.
inline constexpr std::uint64_t automation_grid = 256;
// The most automation events one processor receives in one chunk (chased
// values included). What does not fit arrives in the next chunk.
inline constexpr std::size_t automation_event_budget = 256;

// The strip controls a lane or a move can drive.
inline constexpr std::size_t strip_controls = 3; // gain, pan, mute

// Per track, owned by the engine. The atomics are the strip's live values,
// written by the control thread (SongEngine::set_strip and move()); the rest
// belongs to the render callback alone.
struct StripAutomation {
    // Linear gain without pan, mute or solo.
    std::atomic<float> fader{1.0F};
    // Constant-power pan coefficients.
    std::atomic<float> pan_left{0.70710678F};
    std::atomic<float> pan_right{0.70710678F};
    // 1 while the strip is not muted, 0 while it is.
    std::atomic<float> unmuted{1.0F};
    // 1 unless another track is soloed and this one is not.
    std::atomic<float> solo_gate{1.0F};
    // Callback only: which controls a producer is holding (touch), and which
    // have been touched since the transport started (latch).
    std::array<bool, strip_controls> touched{};
    std::array<bool, strip_controls> latched{};
};

// One track's strip lanes, sampled on the grid. A control without a lane has
// an empty vector and plays its live value.
struct StripEnvelope {
    AutomationMode mode = AutomationMode::read;
    std::vector<float> gain;      // linear
    std::vector<float> unmuted;   // 1 heard, 0 muted
    std::vector<float> pan_left;  // pan coefficients
    std::vector<float> pan_right;
    [[nodiscard]] bool active() const noexcept {
        return !gain.empty() || !unmuted.empty() || !pan_left.empty();
    }
};

// A point of a compiled parameter lane: from `sample` on, the parameter is
// `value` (until the next point).
struct TimedValue {
    std::uint64_t sample = 0;
    double value = 0.0;
};

struct ParameterLaneTimeline {
    std::int32_t parameter = 0;
    AutomationMode mode = AutomationMode::read;
    std::vector<TimedValue> points;
    // Callback only: the plugin's own gesture is holding this parameter, so
    // its lane is not played (touch: while held; latch: until the stop).
    mutable bool held = false;
};

// Every parameter lane of one processor, and the events they make, merged in
// sample order.
struct ProcessorLanes {
    ProcessorAddress where;
    std::vector<ParameterLaneTimeline> lanes;
    std::vector<TimedValue> event_values;       // sorted by sample
    std::vector<std::uint32_t> event_lanes;     // parallel: the lane of each event
    // Callback only: the next event to play, and whether the lanes' values
    // are owed at the next chunk (after a seek, a wrap or a new arrangement).
    mutable std::size_t cursor = 0;
    mutable bool chase = true;
};

// Per arrangement: the compiled automation of every track.
struct ArrangementAutomation {
    std::vector<StripEnvelope> strips; // indexed by track
    std::vector<ProcessorLanes> processors;
    // Index into `processors` of each track's instrument lanes, or -1.
    std::vector<std::int32_t> instrument_lanes;
    bool any_strip = false;
    std::uint64_t song_samples = 0;
};

// Control thread. Compiles the automation of `song` for an arrangement
// `song_samples` long under `clock`.
void compile_automation(ArrangementAutomation& target, const Song& song, const TickClock& clock,
                        std::uint64_t song_samples);

// What chunk stage 9 hands the mix: the strip gain at the chunk's first and
// last frame (linear in between), and what the sends take.
struct StripRamp {
    StripGain from;
    StripGain to;
    float fader_from = 1.0F; // pan-free gain, for post-fader sends
    float fader_to = 1.0F;
    float audible_from = 1.0F; // mute and solo, for pre-fader sends
    float audible_to = 1.0F;
    bool automated = false; // false: `from` is the static gain; no ramp
};

// Chunk stage 9. `fixed` is the static strip gain; it is what a track with no
// strip lane (or with its mode off or write, stopped) plays. `rolling` says
// whether the transport is playing the arrangement.
[[nodiscard]] StripRamp chunk_strip_gain(const StripAutomation& automation,
                                         const ArrangementAutomation& lanes, std::size_t track,
                                         StripGain fixed, std::uint64_t song_position,
                                         std::size_t frames, bool rolling) noexcept;

// A strip move arriving on the callback: sets the touch bit of `control`, and
// its latch bit when the transport is rolling.
void apply_strip_touch(StripAutomation& automation, std::size_t control, bool touching,
                       bool rolling) noexcept;
// The transport stopped: every touch, latch and plugin hold is let go.
void release_automation(StripAutomation& automation) noexcept;
void release_parameter_holds(const ArrangementAutomation& lanes) noexcept;

// The lanes of the processor at `where`, or nullptr.
[[nodiscard]] const ProcessorLanes* lanes_for(const ArrangementAutomation& lanes,
                                              ProcessorAddress where) noexcept;
// Points every processor's cursor at `position` and owes each lane a chase.
void seek_automation(const ArrangementAutomation& lanes, std::uint64_t position) noexcept;

// Writes the automation events `processor` receives in [position, end) into
// `out` (the chased values first, at offset 0), at most out.size(). Returns
// how many.
std::size_t automation_events(const ProcessorLanes& processor, std::uint64_t position,
                              std::uint64_t end, std::span<PluginEvent> out) noexcept;

// A plugin's own edit, seen as it is drained: a gesture on a parameter with a
// lane holds the lane for the lane's mode (touch until its end, latch until
// the stop). Only while the transport plays.
void note_parameter_edit(const ArrangementAutomation& lanes, ProcessorAddress where,
                         const ParameterEdit& edit) noexcept;

// Adds a track into the bus with its gain ramped linearly from `ramp.from`
// at the first frame towards `ramp.to` at the last, and returns the peak it
// contributed.
[[nodiscard]] float mix_into_ramp(StereoBlock bus, std::span<const float> track_left,
                                  std::span<const float> track_right,
                                  const StripRamp& ramp) noexcept;

} // namespace blokkily::engine
