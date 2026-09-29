#include "engine_launcher.hpp"

#include "blokkily/sequencer/scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace blokkily::engine {

namespace {

constexpr std::uint32_t most_loops = 64;
constexpr std::uint32_t scene_field = 0x3FFFU;

// Where session tick `tick` sounds, by the rule the arrangement is compiled
// with.
std::uint64_t sample_of(const TickClock& clock, double offset, Tick tick) noexcept {
    return sample_for_tick(clock, static_cast<double>(tick) - offset);
}

// The first whole session tick that sounds at or after `position`.
Tick first_tick_at(const TickClock& clock, double offset, std::uint64_t position) noexcept {
    auto tick = static_cast<Tick>(std::floor(clock.tick_at(static_cast<double>(position)) + offset));
    // Rounding can put the tick one late; ticks before the song's start all
    // read as its first sample, so the walk back stops there.
    while (tick > 0 && static_cast<double>(tick - 1) - offset >= 0.0 &&
           sample_of(clock, offset, tick - 1) >= position)
        --tick;
    while (sample_of(clock, offset, tick) < position) ++tick;
    return tick;
}

std::uint32_t pack(const LauncherTrack& track) noexcept {
    std::uint32_t word = track.playing ? 1U : 0U;
    if (track.queued == LauncherTrack::Queued::play) word |= 2U;
    if (track.queued == LauncherTrack::Queued::stop) word |= 4U;
    word |= std::min(track.scene, scene_field) << 3U;
    word |= std::min(track.queued_scene, scene_field) << 17U;
    return word;
}

void publish(LauncherTrack& track) noexcept {
    track.status.store(pack(track), std::memory_order_release);
}

std::uint64_t next_random(LauncherPlayback& playback) noexcept {
    // xorshift64*: deterministic from the seed prepare() was given.
    auto x = playback.random;
    x ^= x >> 12U;
    x ^= x << 25U;
    x ^= x >> 27U;
    playback.random = x;
    return x * 0x2545F4914F6CDD1DULL;
}

const LauncherPattern* pattern_of(const ArrangementLauncher& arranged, std::int32_t index) noexcept {
    if (index < 0 || static_cast<std::size_t>(index) >= arranged.patterns.size()) return nullptr;
    const auto& pattern = arranged.patterns[static_cast<std::size_t>(index)];
    if (pattern.loops == 0 || pattern.length <= 0) return nullptr;
    return &pattern;
}

// The context every boundary is played in.
struct Chunk {
    LauncherPlayback& playback;
    const ArrangementLauncher& arranged;
    const TickClock& clock;
    const MeterMap& meter;
};

void close_take(Chunk& chunk, LauncherTrack& track, Tick at) noexcept {
    if (!track.take_open) return;
    track.take_open = false;
    if (at <= track.take.start) return;
    track.take.end = at;
    if (!chunk.playback.takes.push(track.take))
        chunk.playback.dropped_takes.fetch_add(1, std::memory_order_relaxed);
}

void open_take(Chunk& chunk, LauncherTrack& track, std::uint32_t index, Tick at) noexcept {
    if (!chunk.playback.recording.load(std::memory_order_acquire)) return;
    const auto* pattern = pattern_of(chunk.arranged, track.pattern);
    if (pattern == nullptr) return;
    track.take_open = true;
    track.take = {index, track.scene, static_cast<std::size_t>(track.pattern), at, at,
                  pattern->loops};
}

void stop_at(Chunk& chunk, LauncherTrack& track, Tick at) noexcept {
    if (track.playing) {
        track.release = track.release || track.held_count > 0;
        close_take(chunk, track, at);
    }
    track.playing = false;
    track.cursor_valid = false;
}

void launch_at(Chunk& chunk, LauncherTrack& track, std::uint32_t index, std::uint32_t scene,
               Tick at) noexcept {
    const auto* cell = chunk.arranged.cell(scene, index);
    if (cell == nullptr || pattern_of(chunk.arranged, cell->pattern) == nullptr) {
        stop_at(chunk, track, at);
        return;
    }
    if (track.playing) {
        // What the old cell holds is let go on the boundary, as a clip cut
        // there lets it go.
        track.release = track.release || track.held_count > 0;
        close_take(chunk, track, at);
    } else {
        track.took_over = true;
    }
    track.playing = true;
    track.scene = scene;
    track.pattern = cell->pattern;
    track.start = at;
    track.loop = 0;
    track.cursor = 0;
    track.cursor_valid = false;
    open_take(chunk, track, index, at);
}

// Where a follow action goes from `scene` on track `index`.
std::optional<std::uint32_t> follow(Chunk& chunk, FollowAction action, std::uint32_t scene,
                                    std::uint32_t index) noexcept {
    const auto scenes = chunk.arranged.scenes;
    if (scenes == 0) return std::nullopt;
    switch (action) {
    case FollowAction::none:
    case FollowAction::again: return scene;
    case FollowAction::stop: return std::nullopt;
    case FollowAction::next: return (scene + 1) % scenes;
    case FollowAction::previous: return scene > 0 ? scene - 1 : scenes - 1;
    case FollowAction::first: return 0U;
    case FollowAction::last: return scenes - 1;
    case FollowAction::random: {
        // Another scene with a cell on this track; this one if there is none.
        std::uint32_t others = 0;
        for (std::uint32_t s = 0; s < scenes; ++s)
            if (s != scene && chunk.arranged.cell(s, index) != nullptr) ++others;
        if (others == 0) return scene;
        auto choice = static_cast<std::uint32_t>(next_random(chunk.playback) % others);
        for (std::uint32_t s = 0; s < scenes; ++s) {
            if (s == scene || chunk.arranged.cell(s, index) == nullptr) continue;
            if (choice == 0) return s;
            --choice;
        }
        return scene;
    }
    }
    return std::nullopt;
}

void loop_end(Chunk& chunk, LauncherTrack& track, std::uint32_t index, Tick at) noexcept {
    const auto* cell = chunk.arranged.cell(track.scene, index);
    if (cell == nullptr) {
        stop_at(chunk, track, at);
        return;
    }
    ++track.loop;
    if (cell->repeats > 0 && track.loop >= cell->repeats && cell->follow != FollowAction::none) {
        if (cell->follow == FollowAction::stop) {
            stop_at(chunk, track, at);
            return;
        }
        const auto to = follow(chunk, cell->follow, track.scene, index);
        if (to) launch_at(chunk, track, index, *to, at);
        else stop_at(chunk, track, at);
        return;
    }
    track.start = at;
    track.cursor_valid = false;
    // Recording switched on while the cell played: the take starts on a loop
    // boundary that begins the pattern's cycle, so it prints as it sounded.
    if (!track.take_open) {
        const auto* pattern = pattern_of(chunk.arranged, track.pattern);
        if (pattern != nullptr && track.loop % pattern->loops == 0)
            open_take(chunk, track, index, at);
    }
}

// The next boundary of `track` after everything due has been played, as a
// session tick, or nothing.
std::optional<Tick> next_boundary(const Chunk& chunk, const LauncherTrack& track) noexcept {
    std::optional<Tick> at;
    if (track.queued != LauncherTrack::Queued::none) at = track.queued_at;
    if (track.playing) {
        if (const auto* pattern = pattern_of(chunk.arranged, track.pattern)) {
            const Tick end = track.start + pattern->length;
            if (!at || end < *at) at = end;
        }
    }
    return at;
}

void play_due(Chunk& chunk, LauncherTrack& track, std::uint32_t index,
              std::uint64_t position) noexcept {
    // Bounded: a pattern one tick long cannot hold the callback.
    for (int round = 0; round < 256; ++round) {
        const bool queued = track.queued != LauncherTrack::Queued::none;
        const auto* pattern = track.playing ? pattern_of(chunk.arranged, track.pattern) : nullptr;
        if (track.playing && pattern == nullptr) {
            stop_at(chunk, track, track.start);
            continue;
        }
        if (!queued && pattern == nullptr) return;
        const Tick loop = pattern != nullptr ? track.start + pattern->length
                                             : std::numeric_limits<Tick>::max();
        // A launch or stop on the same tick as a loop's end wins.
        const bool transition = queued && track.queued_at <= loop;
        const Tick at = transition ? track.queued_at : loop;
        if (sample_of(chunk.clock, chunk.playback.offset, at) > position) return;
        if (transition) {
            const auto what = track.queued;
            track.queued = LauncherTrack::Queued::none;
            if (what == LauncherTrack::Queued::stop) stop_at(chunk, track, at);
            else launch_at(chunk, track, index, track.queued_scene, at);
        } else {
            loop_end(chunk, track, index, at);
        }
    }
}

void apply(Chunk& chunk, const LauncherCommand& command, Tick now, bool fresh) noexcept {
    auto& playback = chunk.playback;
    const auto quantized = [&](LaunchQuantization quantization) {
        if (fresh || quantization == LaunchQuantization::none) return now;
        return std::max(now, SceneMatrix::next_quantized_tick(now, quantization, chunk.meter));
    };
    const auto queue_stop = [&](LauncherTrack& track, Tick at) {
        if (track.playing) {
            track.queued = LauncherTrack::Queued::stop;
            track.queued_at = at;
        } else {
            // Waiting to start and not started: it simply does not.
            track.queued = LauncherTrack::Queued::none;
        }
    };
    const auto queue_play = [&](LauncherTrack& track, std::uint32_t scene, Tick at) {
        track.queued = LauncherTrack::Queued::play;
        track.queued_scene = scene;
        track.queued_at = at;
    };
    switch (command.type) {
    case LauncherCommand::Type::launch_cell: {
        if (command.track >= playback.track_count) return;
        const auto* cell = chunk.arranged.cell(command.scene, command.track);
        if (cell == nullptr) return;
        queue_play(playback.tracks[command.track], command.scene, quantized(cell->quantization));
        return;
    }
    case LauncherCommand::Type::launch_scene: {
        if (command.scene >= chunk.arranged.scenes) return;
        const Tick at = quantized(chunk.arranged.quantization);
        // A scene plays its row: a track with nothing in it stops.
        for (std::uint32_t index = 0; index < playback.track_count; ++index) {
            auto& track = playback.tracks[index];
            if (chunk.arranged.cell(command.scene, index) != nullptr)
                queue_play(track, command.scene, at);
            else
                queue_stop(track, at);
        }
        return;
    }
    case LauncherCommand::Type::stop_track:
        if (command.track >= playback.track_count) return;
        queue_stop(playback.tracks[command.track], quantized(chunk.arranged.quantization));
        return;
    case LauncherCommand::Type::stop_all: {
        const Tick at = quantized(chunk.arranged.quantization);
        for (std::size_t index = 0; index < playback.track_count; ++index)
            queue_stop(playback.tracks[index], at);
        return;
    }
    }
}

} // namespace

const LauncherCell* ArrangementLauncher::cell(std::uint32_t scene,
                                              std::uint32_t track) const noexcept {
    if (scene >= scenes || track >= tracks) return nullptr;
    const auto& found = cells[static_cast<std::size_t>(scene) * tracks + track];
    if (found.pattern < 0 || static_cast<std::size_t>(found.pattern) >= patterns.size() ||
        patterns[static_cast<std::size_t>(found.pattern)].loops == 0)
        return nullptr;
    return &found;
}

std::uint32_t launcher_loop_cycle(const Pattern& pattern) {
    std::uint32_t cycle = 1;
    bool random = false;
    for (const auto& trigger : pattern.events()) {
        if (trigger.play_on_loop > 1)
            cycle = std::min<std::uint32_t>(most_loops, std::lcm(cycle, trigger.play_on_loop));
        if (trigger.probability > 0.0F && trigger.probability < 1.0F) random = true;
    }
    if (random) cycle = std::min<std::uint32_t>(most_loops, std::lcm(cycle, 16U));
    return cycle;
}

void compile_launcher(ArrangementLauncher& target, const Song& song, std::uint64_t seed) {
    const auto& launcher = song.launcher;
    target.scenes = static_cast<std::uint32_t>(launcher.scenes.size());
    target.tracks = static_cast<std::uint32_t>(song.tracks.size());
    target.quantization = launcher.quantization;
    target.cells.assign(static_cast<std::size_t>(target.scenes) * target.tracks, LauncherCell{});
    std::vector<bool> used(song.patterns.size(), false);
    for (std::uint32_t scene = 0; scene < target.scenes; ++scene)
        for (std::uint32_t track = 0; track < target.tracks; ++track) {
            const auto& slot = launcher.slot(scene, track);
            if (!slot || slot->pattern >= song.patterns.size()) continue;
            target.cells[static_cast<std::size_t>(scene) * target.tracks + track] = {
                static_cast<std::int32_t>(slot->pattern), slot->repeats, slot->quantization,
                slot->follow_action};
            used[slot->pattern] = true;
        }

    const Scheduler scheduler;
    target.patterns.resize(song.patterns.size());
    for (std::size_t index = 0; index < song.patterns.size(); ++index) {
        auto& compiled = target.patterns[index];
        const auto& pattern = song.patterns[index].pattern;
        compiled.length = pattern.length();
        compiled.events.clear();
        compiled.loop_begin.clear();
        compiled.loops = 0;
        if (!used[index] || compiled.length <= 0) continue;
        compiled.loops = launcher_loop_cycle(pattern);
        const auto inside = [&compiled](Tick tick) {
            return std::clamp<Tick>(tick, 0, compiled.length - 1);
        };
        for (std::uint32_t loop = 1; loop <= compiled.loops; ++loop) {
            const auto first = compiled.events.size();
            compiled.loop_begin.push_back(static_cast<std::uint32_t>(first));
            // Loop numbers count from 1 as a clip's repeats do, with the seed
            // the arrangement is compiled with: printed as a clip, each loop
            // plays what it played here.
            const auto rendered = scheduler.render(pattern, loop, seed);
            // Parameters before notes on a tick, as the timeline orders them.
            for (const auto& parameter : rendered.parameters) {
                const auto type = parameter.kind == ParameterLock::Kind::modulation
                                      ? PluginEvent::Type::parameter_modulation
                                      : PluginEvent::Type::parameter_value;
                compiled.events.push_back(
                    {inside(parameter.start), 0, {type, 0, parameter.index, parameter.value}});
            }
            for (const auto& note : rendered.notes)
                compiled.events.push_back({inside(note.start), std::max<Tick>(0, note.duration),
                                           {PluginEvent::Type::note_on, 0, note.key,
                                            note.velocity, note.cents}});
            std::stable_sort(compiled.events.begin() + static_cast<std::ptrdiff_t>(first),
                             compiled.events.end(),
                             [](const LauncherEvent& a, const LauncherEvent& b) {
                                 return a.tick < b.tick;
                             });
        }
        compiled.loop_begin.push_back(static_cast<std::uint32_t>(compiled.events.size()));
    }
}

void prepare_launcher(LauncherPlayback& playback, std::size_t tracks, std::uint64_t seed) {
    playback.tracks = std::make_unique<LauncherTrack[]>(tracks);
    playback.track_count = tracks;
    LauncherCommand dropped;
    while (playback.commands.pop(dropped)) {
    }
    playback.rolling = false;
    playback.offset = 0.0;
    playback.expected = 0;
    playback.expected_session = 0.0;
    playback.expected_tick = 0;
    playback.arranged = nullptr;
    playback.random = seed * 0x9E3779B97F4A7C15ULL + 0x632BE59BD9B4E019ULL;
    if (playback.random == 0) playback.random = 1;
}

void reset_launcher(LauncherPlayback& playback) noexcept {
    for (std::size_t index = 0; index < playback.track_count; ++index) {
        auto& track = playback.tracks[index];
        if (track.take_open) {
            track.take_open = false;
            if (playback.expected_tick > track.take.start) {
                track.take.end = playback.expected_tick;
                if (!playback.takes.push(track.take))
                    playback.dropped_takes.fetch_add(1, std::memory_order_relaxed);
            }
        }
        track.playing = false;
        track.queued = LauncherTrack::Queued::none;
        track.held_count = 0;
        track.release = false;
        track.took_over = false;
        track.cursor_valid = false;
        publish(track);
    }
    LauncherCommand dropped;
    while (playback.commands.pop(dropped)) {
    }
    playback.rolling = false;
}

std::uint64_t begin_launcher_chunk(LauncherPlayback& playback, const ArrangementLauncher& arranged,
                                   const TickClock& clock, const MeterMap& meter,
                                   std::uint64_t position) noexcept {
    bool fresh = false;
    if (!playback.rolling) {
        // The transport has just started: session ticks are the song's.
        playback.rolling = true;
        playback.offset = 0.0;
        fresh = true;
    } else if (position != playback.expected || &arranged != playback.arranged) {
        // A wrap, a seek or a new clock: the session runs on unbroken.
        playback.offset = playback.expected_session - clock.tick_at(static_cast<double>(position));
    }
    Chunk chunk{playback, arranged, clock, meter};
    const Tick now = first_tick_at(clock, playback.offset, position);
    if (&arranged != playback.arranged) {
        playback.arranged = &arranged;
        // The grid or a pattern may have changed under a playing cell: it
        // plays the cell's pattern now, or stops if the cell is gone.
        for (std::uint32_t index = 0; index < playback.track_count; ++index) {
            auto& track = playback.tracks[index];
            track.cursor_valid = false;
            if (!track.playing) continue;
            const auto* cell = arranged.cell(track.scene, index);
            if (cell == nullptr) stop_at(chunk, track, now);
            else track.pattern = cell->pattern;
        }
    }
    LauncherCommand command;
    while (playback.commands.pop(command)) apply(chunk, command, now, fresh);

    // Recording switched off: an open take ends here.
    if (!playback.recording.load(std::memory_order_acquire))
        for (std::uint32_t index = 0; index < playback.track_count; ++index)
            close_take(chunk, playback.tracks[index], now);

    std::uint64_t until = no_launcher_boundary;
    for (std::uint32_t index = 0; index < playback.track_count; ++index) {
        auto& track = playback.tracks[index];
        play_due(chunk, track, index, position);
        publish(track);
        if (const auto at = next_boundary(chunk, track)) {
            const auto sample = sample_of(clock, playback.offset, *at);
            // At least one sample: a chunk must move the playhead.
            const auto frames = sample > position ? sample - position : 1;
            until = std::min(until, frames);
        }
    }
    return until;
}

void end_launcher_chunk(LauncherPlayback& playback, const TickClock& clock,
                        std::uint64_t end) noexcept {
    playback.expected = end;
    playback.expected_session = clock.tick_at(static_cast<double>(end)) + playback.offset;
    playback.expected_tick = first_tick_at(clock, playback.offset, end);
}

void stop_launcher(LauncherPlayback& playback) noexcept {
    if (!playback.rolling) return;
    playback.rolling = false;
    for (std::size_t index = 0; index < playback.track_count; ++index) {
        auto& track = playback.tracks[index];
        if (track.playing) {
            track.release = track.release || track.held_count > 0;
            if (track.take_open) {
                track.take_open = false;
                if (playback.expected_tick > track.take.start) {
                    track.take.end = playback.expected_tick;
                    if (!playback.takes.push(track.take))
                        playback.dropped_takes.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
        track.playing = false;
        track.queued = LauncherTrack::Queued::none;
        track.cursor_valid = false;
        publish(track);
    }
}

bool launcher_owns(const LauncherPlayback& playback, std::size_t track) noexcept {
    if (track >= playback.track_count) return false;
    const auto& state = playback.tracks[track];
    return state.playing || state.release;
}

bool launcher_takes_over(LauncherPlayback& playback, std::size_t track) noexcept {
    if (track >= playback.track_count) return false;
    auto& state = playback.tracks[track];
    const bool owed = state.took_over;
    state.took_over = false;
    return owed;
}

std::size_t launcher_events(LauncherPlayback& playback, const ArrangementLauncher& arranged,
                            const TickClock& clock, std::size_t index, std::uint64_t position,
                            std::uint64_t end, bool rolling, std::span<PluginEvent> out) noexcept {
    if (index >= playback.track_count) return 0;
    auto& track = playback.tracks[index];
    auto& scratch = playback.scratch;
    std::size_t count = 0;
    const auto emit = [&](std::uint64_t sample, std::uint8_t order, const PluginEvent& event) {
        if (count >= scratch.size()) return;
        auto placed = event;
        placed.sample_offset =
            static_cast<std::uint32_t>(sample > position ? sample - position : 0);
        scratch[count++] = {placed.sample_offset, order, placed};
    };
    const auto release_note = [&](std::uint64_t sample, const LauncherNote& note) {
        emit(sample, note.off == note.on ? 3 : 1,
             {PluginEvent::Type::note_off, 0, note.key, 0.0, note.cents});
    };

    if (track.release) {
        for (std::uint32_t held = 0; held < track.held_count; ++held)
            release_note(position, track.held[held]);
        track.held_count = 0;
        track.release = false;
    }
    const auto* pattern = rolling && track.playing ? pattern_of(arranged, track.pattern) : nullptr;
    if (pattern != nullptr) {
        const double offset = playback.offset;
        const auto variant = track.loop % pattern->loops;
        const auto begin = pattern->loop_begin[variant];
        const auto finish = pattern->loop_begin[variant + 1];
        if (!track.cursor_valid) {
            // The first event of the loop that sounds at or after here.
            const auto first = std::partition_point(
                pattern->events.begin() + begin, pattern->events.begin() + finish,
                [&](const LauncherEvent& event) {
                    return sample_of(clock, offset, track.start + event.tick) < position;
                });
            track.cursor = static_cast<std::uint32_t>(first - pattern->events.begin());
            track.cursor_valid = true;
        }
        while (track.cursor < finish) {
            const auto& event = pattern->events[track.cursor];
            const Tick at = track.start + event.tick;
            const auto sample = sample_of(clock, offset, at);
            if (sample >= end) break;
            ++track.cursor;
            if (event.event.type == PluginEvent::Type::note_on) {
                if (track.held_count >= track.held.size()) continue;
                track.held[track.held_count++] = {event.event.key_or_parameter, event.event.cents,
                                                  at, at + event.duration};
                emit(sample, 2, event.event);
            } else {
                emit(sample, 0, event.event);
            }
        }
        // Releases that fall in this chunk, in the order their notes began.
        std::uint32_t kept = 0;
        for (std::uint32_t held = 0; held < track.held_count; ++held) {
            const auto& note = track.held[held];
            const auto sample = sample_of(clock, offset, note.off);
            if (sample < end) release_note(sample, note);
            else track.held[kept++] = note;
        }
        track.held_count = kept;
    }

    // Time order, stably: what sounds on one sample keeps the order the
    // arrangement's timeline gives it.
    for (std::size_t i = 1; i < count; ++i) {
        const auto moving = scratch[i];
        std::size_t j = i;
        while (j > 0 && (scratch[j - 1].offset > moving.offset ||
                         (scratch[j - 1].offset == moving.offset &&
                          scratch[j - 1].order > moving.order))) {
            scratch[j] = scratch[j - 1];
            --j;
        }
        scratch[j] = moving;
    }
    const auto written = std::min(count, out.size());
    for (std::size_t i = 0; i < written; ++i) out[i] = scratch[i].event;
    return written;
}

LauncherTrackStatus launcher_status(const LauncherPlayback& playback, std::size_t track) noexcept {
    if (track >= playback.track_count) return {};
    const auto word = playback.tracks[track].status.load(std::memory_order_acquire);
    LauncherTrackStatus status;
    status.playing = (word & 1U) != 0;
    status.queued_play = (word & 2U) != 0;
    status.queued_stop = (word & 4U) != 0;
    status.scene = (word >> 3U) & scene_field;
    status.queued_scene = (word >> 17U) & scene_field;
    return status;
}

} // namespace blokkily::engine
