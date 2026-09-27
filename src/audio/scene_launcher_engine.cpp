#include "blokkily/audio/scene_launcher_engine.hpp"
#include "blokkily/sequencer/scheduler.hpp"

#include <algorithm>
#include <cmath>

namespace blokkily {

void SceneLauncherEngine::prepare(const Song& song) {
    matrix_ = song.launcher;
    tracks_.clear();
    tracks_.resize(song.tracks.size());

    patterns_.clear();
    patterns_.resize(song.patterns.size());

    const Scheduler scheduler;
    for (std::size_t p = 0; p < song.patterns.size(); ++p) {
        const auto& pat = song.patterns[p].pattern;
        CompiledPattern cp;
        cp.length = pat.length();

        // Compile 1 loop with seed 0
        const auto compiled = scheduler.render(pat, 1, 0);

        for (const auto& note : compiled.notes) {
            PluginEvent note_on;
            note_on.type = PluginEvent::Type::note_on;
            note_on.sample_offset = 0;
            note_on.key_or_parameter = note.key;
            note_on.value = static_cast<double>(note.velocity);
            note_on.cents = note.cents;
            cp.events.push_back({note.start, note_on});

            PluginEvent note_off;
            note_off.type = PluginEvent::Type::note_off;
            note_off.sample_offset = 0;
            note_off.key_or_parameter = note.key;
            note_off.value = 0.0;
            note_off.cents = 0.0;
            cp.events.push_back({note.start + note.duration, note_off});
        }

        for (const auto& param : compiled.parameters) {
            PluginEvent param_val;
            param_val.type = PluginEvent::Type::parameter_value;
            param_val.sample_offset = 0;
            param_val.key_or_parameter = param.index;
            param_val.value = param.value;
            param_val.cents = 0.0;
            cp.events.push_back({param.start, param_val});
        }

        const auto by_tick = [](const TimedTickEvent& a, const TimedTickEvent& b) {
            return a.tick < b.tick;
        };
        std::stable_sort(cp.events.begin(), cp.events.end(), by_tick);
        patterns_[p] = std::move(cp);
    }

    cmd_read_.store(0, std::memory_order_relaxed);
    cmd_write_.store(0, std::memory_order_relaxed);
    jam_count_.store(0, std::memory_order_relaxed);
}

bool SceneLauncherEngine::push_command(const LaunchCommand& cmd) noexcept {
    const std::size_t write = cmd_write_.load(std::memory_order_relaxed);
    const std::size_t read = cmd_read_.load(std::memory_order_acquire);
    const std::size_t next = (write + 1) % cmd_queue_capacity;
    if (next == read) return false; // Queue full

    cmd_queue_[write] = cmd;
    cmd_write_.store(next, std::memory_order_release);
    return true;
}

void SceneLauncherEngine::launch_slot(std::size_t track, std::size_t scene_index,
                                     LaunchQuantization q) {
    push_command(LaunchCommand{
        .type = LaunchCommand::Type::launch_slot,
        .track = track,
        .scene = scene_index,
        .quantization = q
    });
}

void SceneLauncherEngine::launch_scene(std::size_t scene_index,
                                      LaunchQuantization q) {
    push_command(LaunchCommand{
        .type = LaunchCommand::Type::launch_scene,
        .track = 0,
        .scene = scene_index,
        .quantization = q
    });
}

void SceneLauncherEngine::stop_track(std::size_t track,
                                    LaunchQuantization q) {
    push_command(LaunchCommand{
        .type = LaunchCommand::Type::stop_track,
        .track = track,
        .scene = 0,
        .quantization = q
    });
}

void SceneLauncherEngine::stop_all(LaunchQuantization q) {
    push_command(LaunchCommand{
        .type = LaunchCommand::Type::stop_all,
        .track = 0,
        .scene = 0,
        .quantization = q
    });
}

bool SceneLauncherEngine::is_playing(std::size_t track) const noexcept {
    if (track >= tracks_.size()) return false;
    return tracks_[track].status == TrackStatus::playing;
}

bool SceneLauncherEngine::is_queued(std::size_t track) const noexcept {
    if (track >= tracks_.size()) return false;
    return tracks_[track].status == TrackStatus::queued_play ||
           tracks_[track].status == TrackStatus::queued_stop;
}

std::size_t SceneLauncherEngine::active_scene(std::size_t track) const noexcept {
    if (track >= tracks_.size()) return 0;
    return tracks_[track].active_scene;
}

std::size_t SceneLauncherEngine::queued_scene(std::size_t track) const noexcept {
    if (track >= tracks_.size()) return 0;
    return tracks_[track].queued_scene;
}

std::vector<JamRecord> SceneLauncherEngine::take_jam_records() {
    const std::size_t count = jam_count_.load(std::memory_order_acquire);
    std::vector<JamRecord> out;
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        out.push_back(completed_jams_[i]);
    }
    jam_count_.store(0, std::memory_order_release);
    return out;
}

void SceneLauncherEngine::close_jam_for_track(std::size_t track, Tick end_tick) noexcept {
    if (track >= tracks_.size()) return;
    auto& t = tracks_[track];
    if (t.current_jam.has_value()) {
        auto jam = *t.current_jam;
        jam.end_tick = end_tick;
        if (jam.end_tick > jam.start_tick) {
            const std::size_t idx = jam_count_.load(std::memory_order_relaxed);
            if (idx < max_jams) {
                completed_jams_[idx] = jam;
                jam_count_.store(idx + 1, std::memory_order_release);
            }
        }
        t.current_jam.reset();
    }
}

void SceneLauncherEngine::apply_commands(Tick current_tick, const MeterMap& meter) noexcept {
    std::size_t read = cmd_read_.load(std::memory_order_relaxed);
    const std::size_t write = cmd_write_.load(std::memory_order_acquire);

    while (read != write) {
        const auto& cmd = cmd_queue_[read];
        read = (read + 1) % cmd_queue_capacity;

        const Tick target_tick = SceneMatrix::next_quantized_tick(current_tick, cmd.quantization, meter);
        const bool immediate = (cmd.quantization == LaunchQuantization::none) || (target_tick <= current_tick);

        switch (cmd.type) {
            case LaunchCommand::Type::launch_slot: {
                if (cmd.track < tracks_.size()) {
                    auto& t = tracks_[cmd.track];
                    const auto& s = matrix_.slot(cmd.scene, cmd.track);
                    if (s.has_value() && s->pattern < patterns_.size()) {
                        if (immediate) {
                            close_jam_for_track(cmd.track, current_tick);
                            if (jam_recording_.load(std::memory_order_relaxed)) {
                                t.current_jam = JamRecord{cmd.track, s->pattern, current_tick, 0};
                            }
                            t.active_scene = cmd.scene;
                            t.active_pattern = s->pattern;
                            t.total_repeats = s->repeats;
                            t.follow_action = s->follow_action;
                            t.pattern_start_tick = current_tick;
                            t.current_repeat = 0;
                            t.event_cursor = 0;
                            t.status = TrackStatus::playing;
                        } else {
                            t.queued_scene = cmd.scene;
                            t.queued_pattern = s->pattern;
                            t.queued_repeats = s->repeats;
                            t.queued_follow = s->follow_action;
                            t.quantization = cmd.quantization;
                            t.target_launch_tick = target_tick;
                            t.status = TrackStatus::queued_play;
                        }
                    }
                }
                break;
            }

            case LaunchCommand::Type::launch_scene: {
                for (std::size_t trk = 0; trk < tracks_.size(); ++trk) {
                    auto& t = tracks_[trk];
                    const auto& s = matrix_.slot(cmd.scene, trk);
                    if (s.has_value() && s->pattern < patterns_.size()) {
                        if (immediate) {
                            close_jam_for_track(trk, current_tick);
                            if (jam_recording_.load(std::memory_order_relaxed)) {
                                t.current_jam = JamRecord{trk, s->pattern, current_tick, 0};
                            }
                            t.active_scene = cmd.scene;
                            t.active_pattern = s->pattern;
                            t.total_repeats = s->repeats;
                            t.follow_action = s->follow_action;
                            t.pattern_start_tick = current_tick;
                            t.current_repeat = 0;
                            t.event_cursor = 0;
                            t.status = TrackStatus::playing;
                        } else {
                            t.queued_scene = cmd.scene;
                            t.queued_pattern = s->pattern;
                            t.queued_repeats = s->repeats;
                            t.queued_follow = s->follow_action;
                            t.quantization = cmd.quantization;
                            t.target_launch_tick = target_tick;
                            t.status = TrackStatus::queued_play;
                        }
                    }
                }
                break;
            }

            case LaunchCommand::Type::stop_track: {
                if (cmd.track < tracks_.size()) {
                    auto& t = tracks_[cmd.track];
                    if (immediate) {
                        close_jam_for_track(cmd.track, current_tick);
                        t.status = TrackStatus::stopped;
                    } else {
                        t.target_launch_tick = target_tick;
                        t.status = TrackStatus::queued_stop;
                    }
                }
                break;
            }

            case LaunchCommand::Type::stop_all: {
                for (std::size_t trk = 0; trk < tracks_.size(); ++trk) {
                    auto& t = tracks_[trk];
                    if (immediate) {
                        close_jam_for_track(trk, current_tick);
                        t.status = TrackStatus::stopped;
                    } else {
                        t.target_launch_tick = target_tick;
                        t.status = TrackStatus::queued_stop;
                    }
                }
                break;
            }

            case LaunchCommand::Type::none:
                break;
        }
    }
    cmd_read_.store(read, std::memory_order_release);
}

void SceneLauncherEngine::begin_chunk(Tick start_tick, const MeterMap& meter) noexcept {
    apply_commands(start_tick, meter);
}

std::size_t SceneLauncherEngine::process_track(std::size_t track,
                                              std::uint64_t song_position,
                                              std::size_t frames,
                                              const TickClock& clock,
                                              const MeterMap& meter,
                                              std::span<PluginEvent> track_events,
                                              std::size_t current_count,
                                              std::size_t capacity) noexcept {
    if (track >= tracks_.size()) return 0;
    (void)meter;
    auto& t = tracks_[track];

    const Tick start_tick = static_cast<Tick>(clock.tick_at(static_cast<double>(song_position)));
    const Tick end_tick = static_cast<Tick>(clock.tick_at(static_cast<double>(song_position + frames)));

    std::size_t added = 0;

    // 1. Process queued transitions if their boundary arrives in this window
    if (t.status == TrackStatus::queued_stop) {
        if (t.target_launch_tick <= end_tick) {
            close_jam_for_track(track, t.target_launch_tick);
            // Send note offs for all sounding notes
            for (std::size_t k = 0; k < t.sounding.size(); ++k) {
                if (t.sounding[k] > 0 && current_count + added < capacity) {
                    track_events[current_count + added] = {
                        PluginEvent::Type::note_off, 0, static_cast<std::int32_t>(k), 0.0, 0.0
                    };
                    t.sounding[k] = 0;
                    ++added;
                }
            }
            t.status = TrackStatus::stopped;
        }
    } else if (t.status == TrackStatus::queued_play) {
        if (t.target_launch_tick <= end_tick) {
            close_jam_for_track(track, t.target_launch_tick);
            if (jam_recording_.load(std::memory_order_relaxed)) {
                t.current_jam = JamRecord{track, t.queued_pattern, t.target_launch_tick, 0};
            }
            // Send note offs for previously sounding notes
            for (std::size_t k = 0; k < t.sounding.size(); ++k) {
                if (t.sounding[k] > 0 && current_count + added < capacity) {
                    track_events[current_count + added] = {
                        PluginEvent::Type::note_off, 0, static_cast<std::int32_t>(k), 0.0, 0.0
                    };
                    t.sounding[k] = 0;
                    ++added;
                }
            }
            t.active_scene = t.queued_scene;
            t.active_pattern = t.queued_pattern;
            t.total_repeats = t.queued_repeats;
            t.follow_action = t.queued_follow;
            t.pattern_start_tick = t.target_launch_tick;
            t.current_repeat = 0;
            t.event_cursor = 0;
            t.status = TrackStatus::playing;
        }
    }

    if (t.status != TrackStatus::playing) return added;
    if (t.active_pattern >= patterns_.size()) return added;

    const auto& cp = patterns_[t.active_pattern];
    if (cp.length <= 0) return added;

    // 2. Play scheduled events within the current chunk
    while (t.event_cursor < cp.events.size()) {
        const auto& ev = cp.events[t.event_cursor];
        const Tick ev_tick = t.pattern_start_tick + ev.tick;

        if (ev_tick < end_tick) {
            if (ev_tick >= start_tick && current_count + added < capacity) {
                const double sample = clock.sample_at_tick(static_cast<double>(ev_tick));
                const std::uint32_t offset = (sample >= song_position && sample < song_position + frames)
                                                 ? static_cast<std::uint32_t>(sample - song_position)
                                                 : 0;
                PluginEvent out_ev = ev.event;
                out_ev.sample_offset = offset;
                track_events[current_count + added] = out_ev;

                const auto key = out_ev.key_or_parameter;
                if (key >= 0 && key < 128) {
                    if (out_ev.type == PluginEvent::Type::note_on) {
                        t.sounding[static_cast<std::size_t>(key)] = 1;
                    } else if (out_ev.type == PluginEvent::Type::note_off) {
                        t.sounding[static_cast<std::size_t>(key)] = 0;
                    }
                }
                ++added;
            }
            ++t.event_cursor;
        } else {
            break;
        }
    }

    // 3. Loop boundary and Follow Action evaluation
    const Tick loop_end_tick = t.pattern_start_tick + cp.length;
    if (loop_end_tick <= end_tick) {
        ++t.current_repeat;
        if (t.total_repeats > 0 && t.current_repeat >= t.total_repeats) {
            // Repeats finished; resolve follow action
            if (t.follow_action == FollowAction::stop) {
                close_jam_for_track(track, loop_end_tick);
                for (std::size_t k = 0; k < t.sounding.size(); ++k) {
                    if (t.sounding[k] > 0 && current_count + added < capacity) {
                        track_events[current_count + added] = {
                            PluginEvent::Type::note_off, 0, static_cast<std::int32_t>(k), 0.0, 0.0
                        };
                        t.sounding[k] = 0;
                        ++added;
                    }
                }
                t.status = TrackStatus::stopped;
            } else if (t.follow_action == FollowAction::again) {
                t.current_repeat = 0;
                t.pattern_start_tick = loop_end_tick;
                t.event_cursor = 0;
            } else {
                const auto next_scene = matrix_.resolve_follow_action(t.follow_action, t.active_scene, track, 42);
                if (next_scene.has_value()) {
                    const auto& next_slot = matrix_.slot(*next_scene, track);
                    if (next_slot.has_value() && next_slot->pattern < patterns_.size()) {
                        close_jam_for_track(track, loop_end_tick);
                        if (jam_recording_.load(std::memory_order_relaxed)) {
                            t.current_jam = JamRecord{track, next_slot->pattern, loop_end_tick, 0};
                        }
                        t.active_scene = *next_scene;
                        t.active_pattern = next_slot->pattern;
                        t.total_repeats = next_slot->repeats;
                        t.follow_action = next_slot->follow_action;
                        t.pattern_start_tick = loop_end_tick;
                        t.current_repeat = 0;
                        t.event_cursor = 0;
                    } else {
                        t.status = TrackStatus::stopped;
                    }
                } else {
                    t.status = TrackStatus::stopped;
                }
            }
        } else {
            // Normal loop continuation
            t.pattern_start_tick = loop_end_tick;
            t.event_cursor = 0;
        }
    }

    return added;
}

} // namespace blokkily
