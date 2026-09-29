#pragma once

// The scene launcher in the engine (phase 2, wave 6.1). Private to SongEngine.
//
// What a cell plays is compiled from the song on the control thread with the
// rest of the arrangement (ArrangementLauncher) and reaches the render
// callback in the same slot, through the same handoff word: the callback
// never sees the grid or a pattern change under it. Launches and stops are
// commands on a lock-free queue the callback drains at the start of a chunk;
// what each track is doing comes back as one atomic word per track, and the
// takes arrangement recording makes come back on a second queue. Nothing here
// allocates, locks or logs on the callback.
//
// Time. The launcher counts "session" ticks: the song's ticks from where the
// transport started rolling, running on without a break when the song wraps
// or the playhead is moved, so a launched loop keeps its phase whatever the
// arrangement does under it. offset is the session tick less the song tick;
// it is 0 until the first wrap or seek, so a take recorded before one lands on
// exactly the ticks it was heard at. An event at session tick T sounds at
// sample_for_tick(clock, T - offset), the rule the arrangement's timeline is
// compiled with.
//
// Boundaries. A quantized launch or stop, and the end of every loop of a
// playing cell, is a boundary. The engine ends a chunk on the next boundary,
// so a boundary always falls on the first sample of a chunk and a chunk plays
// one loop of one cell per track. At a transition the notes the old cell
// still holds are let go on that sample, as a clip cut there would let them
// go; at the end of a loop they ring on into the next, as they do between the
// repeats of a clip.
//
// A launched track plays its cell instead of its arrangement, whose notes are
// let go when the launcher takes the track over. Stopping the transport stops
// every launched track.

#include "blokkily/audio/event_queue.hpp"
#include "blokkily/audio/scene_launcher_engine.hpp"
#include "blokkily/model/scene_launcher.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/model/timebase.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace blokkily::engine {

// One event of a compiled loop, at a tick of the loop. A note-on carries its
// duration; its note-off is played from what the track holds, so it can fall
// in a later loop or be cut by a transition.
struct LauncherEvent {
    Tick tick = 0;
    Tick duration = 0;
    PluginEvent event{};
};

// A pattern as the launcher plays it: `loops` loops, one after another in
// `events` (loop i runs from loop_begin[i] to loop_begin[i + 1]). A pattern
// whose steps all play every loop compiles one; one with loop conditions or
// probability compiles the cycle it repeats in (launcher_loop_cycle).
struct LauncherPattern {
    Tick length = 0;
    std::uint32_t loops = 0;
    std::vector<LauncherEvent> events;
    std::vector<std::uint32_t> loop_begin;
};

struct LauncherCell {
    std::int32_t pattern = -1;
    std::uint32_t repeats = 0;
    LaunchQuantization quantization = LaunchQuantization::bar;
    FollowAction follow = FollowAction::none;
};

// Compiled with the arrangement; read-only to the callback.
struct ArrangementLauncher {
    std::uint32_t scenes = 0;
    std::uint32_t tracks = 0;
    std::vector<LauncherCell> cells; // scene-major
    std::vector<LauncherPattern> patterns; // indexed like Song::patterns
    LaunchQuantization quantization = LaunchQuantization::bar;

    // The cell, or nullptr when it is empty or names nothing playable.
    [[nodiscard]] const LauncherCell* cell(std::uint32_t scene, std::uint32_t track) const noexcept;
};

// How many loops of `pattern` the launcher compiles: the least common
// multiple of its steps' play_on_loop, raised to a multiple of 16 when a step
// has a probability, and at most 64.
[[nodiscard]] std::uint32_t launcher_loop_cycle(const Pattern& pattern);

// Control thread.
void compile_launcher(ArrangementLauncher& target, const Song& song, std::uint64_t seed);

struct LauncherCommand {
    enum class Type : std::uint8_t { launch_cell, launch_scene, stop_track, stop_all };
    Type type = Type::launch_cell;
    std::uint32_t track = 0;
    std::uint32_t scene = 0;
};

// A note a launched cell is holding, in session ticks.
struct LauncherNote {
    std::int32_t key = 0;
    double cents = 0.0;
    Tick on = 0;
    Tick off = 0;
};
inline constexpr std::size_t launcher_notes_held = 128;

struct LauncherTrack {
    // Callback only.
    bool playing = false;
    std::uint32_t scene = 0;
    std::int32_t pattern = -1;
    Tick start = 0;         // session tick the loop playing began
    std::uint32_t loop = 0; // loops finished since the cell was launched
    std::uint32_t cursor = 0;
    bool cursor_valid = false;
    enum class Queued : std::uint8_t { none, play, stop };
    Queued queued = Queued::none;
    std::uint32_t queued_scene = 0;
    Tick queued_at = 0;
    std::array<LauncherNote, launcher_notes_held> held{};
    std::uint32_t held_count = 0;
    // What is held is let go on the first sample of the next chunk.
    bool release = false;
    // The arrangement's notes on this track are owed a release.
    bool took_over = false;
    bool take_open = false;
    LauncherTake take{};
    // Written by the callback, read by the control thread.
    std::atomic<std::uint32_t> status{0};
};

// One event on its way to a track, with what orders it among events on the
// same sample: parameters, then releases, then note-ons, then the release of
// a note that began on that very sample.
struct LauncherOut {
    std::uint32_t offset = 0;
    std::uint8_t order = 0;
    PluginEvent event{};
};

struct LauncherPlayback {
    std::unique_ptr<LauncherTrack[]> tracks;
    std::size_t track_count = 0;
    SpscQueue<LauncherCommand, 256> commands;
    SpscQueue<LauncherTake, 1024> takes;
    std::atomic<bool> recording{false};
    std::atomic<std::uint32_t> dropped_takes{0};

    // Callback only.
    bool rolling = false;
    double offset = 0.0;
    std::uint64_t expected = 0;       // where the next chunk continues from
    double expected_session = 0.0;    // its session tick
    Tick expected_tick = 0;           // the first whole session tick at or after it
    const ArrangementLauncher* arranged = nullptr;
    std::uint64_t random = 0x9E3779B97F4A7C15ULL;
    std::array<LauncherOut, 512> scratch{};
};

inline constexpr std::uint64_t no_launcher_boundary = std::numeric_limits<std::uint64_t>::max();

// Control thread, with the callback not running: room for `tracks` tracks,
// everything stopped, the random follow action seeded.
void prepare_launcher(LauncherPlayback& playback, std::size_t tracks, std::uint64_t seed);
// Control thread, with the callback not running (a bounce): every track
// stops at once and its notes are forgotten (the instruments were reset),
// open takes are closed where the transport last was, and commands not yet
// played are dropped.
void reset_launcher(LauncherPlayback& playback) noexcept;

// Callback, at the start of every chunk the transport rolls through, before
// the chunk's length is fixed: places the chunk in session time, plays the
// commands and every boundary due at `position`, and returns how many samples
// the chunk may run before the next boundary (no_launcher_boundary if none).
std::uint64_t begin_launcher_chunk(LauncherPlayback& playback, const ArrangementLauncher& arranged,
                                   const TickClock& clock, const MeterMap& meter,
                                   std::uint64_t position) noexcept;
// Callback, after the chunk: where the next one continues from.
void end_launcher_chunk(LauncherPlayback& playback, const TickClock& clock,
                        std::uint64_t end) noexcept;
// Callback, when the transport stops: every track stops where the last chunk
// ended and lets go of its notes in the next chunk rendered.
void stop_launcher(LauncherPlayback& playback) noexcept;

// Whether the launcher, not the arrangement, feeds `track` this chunk.
[[nodiscard]] bool launcher_owns(const LauncherPlayback& playback, std::size_t track) noexcept;
// Whether the arrangement's notes on `track` are owed a release (once).
[[nodiscard]] bool launcher_takes_over(LauncherPlayback& playback, std::size_t track) noexcept;
// Callback: the events `track` plays in [position, end), in time order, into
// `out`; how many. Stopped (`rolling` false), only the notes owed a release.
std::size_t launcher_events(LauncherPlayback& playback, const ArrangementLauncher& arranged,
                            const TickClock& clock, std::size_t track, std::uint64_t position,
                            std::uint64_t end, bool rolling, std::span<PluginEvent> out) noexcept;

// Any thread: a track's status word, unpacked.
[[nodiscard]] LauncherTrackStatus launcher_status(const LauncherPlayback& playback,
                                                  std::size_t track) noexcept;

} // namespace blokkily::engine
