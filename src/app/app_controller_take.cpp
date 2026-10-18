#include "app_controller.hpp"
#include "blokkily/audio/comping.hpp"
#include "blokkily/instruments/soundfont_catalog.hpp"
#include "blokkily/instruments/soundfont_synth.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/midi/input_routes.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QVariantMap>
#include <QUrl>

#include <cstring>
#include <system_error>

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

// ---- MIDI input and recording ------------------------------------------------

QString AppController::midiPort() const {
    return midi_input_->is_open() ? QString::fromStdString(midi_input_->port_name()) : QString();
}

bool AppController::mpe() const { return midi_input_->mpe().enabled; }

namespace {
// Every member channel of a zone: the lower zone is master 1 with members
// 2-16, the upper master 16 with members 1-15 (0-based here).
blokkily::MpeZone full_zone(bool upper, double pitch_range) {
    return upper ? blokkily::MpeZone{true, 15, 0, 14, pitch_range}
                 : blokkily::MpeZone{true, 0, 1, 15, pitch_range};
}
} // namespace

void AppController::setMpe(bool enabled) {
    auto zone = midi_input_->mpe();
    const bool upper = zone.master == 15;
    if (enabled) zone = full_zone(upper, zone.pitch_range);
    else zone.enabled = false;
    midi_input_->set_mpe(zone);
    status_ = !enabled ? QStringLiteral("MIDI · MPE off")
              : upper  ? QStringLiteral("MIDI · MPE on, upper zone: channels 1-15 carry one note each")
                       : QStringLiteral("MIDI · MPE on: channels 2-16 carry one note each");
    emit statusChanged();
    emit midiChanged();
}

bool AppController::mpeUpper() const { return midi_input_->mpe().master == 15; }

void AppController::setMpeUpper(bool upper) {
    auto zone = midi_input_->mpe();
    if ((zone.master == 15) == upper) return;
    const bool enabled = zone.enabled;
    zone = full_zone(upper, zone.pitch_range);
    zone.enabled = enabled;
    midi_input_->set_mpe(zone);
    if (enabled) setMpe(true);   // says which channels now carry notes
    else emit midiChanged();
}

void AppController::refreshMidiPorts() {
    QStringList ports;
    if (midi_input_->mode() == blokkily::MidiInput::Mode::deterministic) {
        ports << QStringLiteral("Deterministic input");
    } else {
        for (const auto& name : midi_input_->ports()) ports << QString::fromStdString(name);
    }
    // The outputs outboard gear is reached through, listed the same way.
    QStringList outputs;
    blokkily::MidiOutput probe(midi_input_->mode() == blokkily::MidiInput::Mode::deterministic
                                   ? blokkily::MidiOutput::Mode::deterministic
                                   : blokkily::MidiOutput::Mode::device);
    for (const auto& name : probe.ports()) outputs << QString::fromStdString(name);
    if (ports == midi_ports_ && outputs == midi_out_ports_) return;
    midi_ports_ = ports;
    midi_out_ports_ = outputs;
    emit midiChanged();
    // Gear plugged in after launch gets the clock it had last time.
    restoreMidiClock();
}

bool AppController::selectMidiPort(int index) {
    if (index < 0) {
        midi_input_->close();
        emit midiChanged();
        return true;
    }
    std::string error;
    if (!midi_input_->open(static_cast<std::size_t>(index), &error)) {
        status_ = QString("MIDI input unavailable · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        emit midiChanged();
        return false;
    }
    // A keyboard is plugged in to be heard: the song gets an engine and the
    // device runs, so the first key sounds without Play being pressed first.
    if (!engine_ && !instruments().empty()) (void)rebuildEngine();
    (void)ensureAudioRunning();
    meter_timer_.start();
    status_ = QString("MIDI · %1").arg(midiPort());
    emit statusChanged();
    emit midiChanged();
    return true;
}

bool AppController::ensureAudioRunning() {
    if (!engine_ || !audio_output_ || !audio_output_->is_open()) return false;
    if (audio_output_->is_running()) return true;
    std::string error;
    if (!audio_output_->start(&error)) {
        status_ = QString("Audio start failed · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        return false;
    }
    meter_timer_.start();
    return true;
}

void AppController::updateKeyMap() {
    if (song_ == nullptr) return;
    // Key n of the controller is degree n of the song, the way the on-screen
    // piano lays one key per degree, with auto-scale having its say first.
    blokkily::KeyMap map{};
    for (int key = 0; key < static_cast<int>(map.size()); ++key) {
        const auto pitch = song_->pitchForDegree(song_->snapDegree(key));
        map[static_cast<std::size_t>(key)] = {pitch.key, static_cast<float>(pitch.cents)};
    }
    midi_input_->set_key_map(map);
}

void AppController::toggleRecord() {
    // Disarming ends the take where it is; arming starts a new one.
    if (record_armed_) finishTake();
    record_armed_ = !record_armed_;
    takes_.clear();
    forgetLapTakes();
    if (song_ != nullptr) song_->newTake();
    if (engine_) engine_->set_recording(record_armed_);
    if (record_armed_) startAudioTake();
    status_ = record_armed_ ? QStringLiteral("Recording armed · play to record")
                            : QStringLiteral("Recording off");
    emit statusChanged();
    emit recordChanged();
    emitBusEvent(blokkily::guide::EventKind::recording_toggled, "");
}

bool AppController::recordingLive() const noexcept {
    return record_armed_ && engine_ != nullptr && engine_->is_playing();
}

void AppController::updateInputRoutes() {
    if (song_ == nullptr) return;
    midi_input_->set_routes(blokkily::midi_routes(
        song_->song(), static_cast<std::size_t>(std::max(0, song_->selectedTrack()))));
}

bool AppController::performPitches(const std::vector<blokkily::TunedPitch>& pitches,
                                   double velocity, bool held) {
    if (!recordingLive() || song_ == nullptr || pitches.empty()) return false;
    // A run of presses does not pile voices up: what the surfaces were still
    // holding is let go first, on the tracks it went down on.
    audition_timer_.stop();
    releaseSoundingNotes();
    const auto routes = blokkily::surface_routes(
        song_->song(), static_cast<std::size_t>(std::max(0, song_->selectedTrack())));
    for (const auto& pitch : pitches) {
        PerformedNote note{pitch, {}};
        for (std::size_t track = 0; track < engine_->track_count(); ++track)
            if ((routes & blokkily::track_bit(track)) != 0 &&
                engine_->perform(track, {blokkily::PluginEvent::Type::note_on, 0, pitch.key,
                                         velocity, pitch.cents}))
                note.tracks.push_back(track);
        if (!note.tracks.empty()) performed_.push_back(std::move(note));
    }
    audition_timer_.start(held ? 8000 : 450);
    meter_timer_.start();
    // Recording against the transport is still recording: the press is the
    // take's, and was not written onto a step.
    return true;
}

bool AppController::performKey(int key, bool held) {
    return performPitches({{static_cast<std::int16_t>(qBound(0, key, 127)), 0.0}}, 0.9, held);
}

void AppController::releasePerformed() {
    if (engine_ != nullptr)
        for (const auto& note : performed_)
            for (const auto track : note.tracks)
                (void)engine_->perform(track, {blokkily::PluginEvent::Type::note_off, 0,
                                               note.pitch.key, 0.0, note.pitch.cents});
    performed_.clear();
}

void AppController::drainTake() {
    if (!engine_ || song_ == nullptr || engine_->sample_rate() <= 0.0) return;
    // The strip moves played since the last drain (item 3.1), recorded into
    // the take's automation passes; a pass a touch let go of is written now,
    // so its lane plays from the next pass round.
    drainAutomation();
    // Every event carries the tick the engine stamped on it, read through the
    // clock the audio thread was playing at the time, so a take played across
    // a tempo change lands on the ticks that were heard, and a tempo edit
    // compiled before this drain cannot move a note already heard.
    // Everything is taken off the engine before anything is written, because
    // writing reaches the engine again and must not find this half done.
    std::vector<blokkily::CapturedEvent> heard;
    blokkily::CapturedEvent captured;
    while (engine_->take_captured(captured)) heard.push_back(captured);
    if (heard.empty()) return;
    const auto length = song_->song().length();
    std::vector<std::pair<std::size_t, blokkily::PlayedNote>> finished;
    std::vector<std::pair<std::size_t, blokkily::ContinuousEvent>> controls;
    // Looped with takes on, what is played inside the loop's range is a take
    // of its pass (features/comping_and_punch.feature); outside it, and
    // without takes, it merges into the pattern played there, as before.
    const auto& loop = song_->song().loop;
    const bool laps = loop.active() && loop.midi_takes;
    const blokkily::Tick span = loop.end - loop.start;
    std::vector<LapNote> lap_finished;
    std::vector<LapControl> lap_controls;
    for (const auto& event : heard) {
        const std::size_t track = event.track;
        if (takes_.size() <= track) takes_.resize(track + 1, blokkily::TakeRecorder(length));
        auto at = event.tick;
        const auto key = static_cast<std::int16_t>(event.event.key_or_parameter);
        if (laps) {
            auto lap = event.lap;
            // Heard just past the loop's end, it is the start of the next pass.
            if (at >= loop.end && at < loop.end + span) {
                at -= span;
                ++lap;
            }
            if (lap_takes_.size() <= track) {
                lap_takes_.resize(track + 1, blokkily::TakeRecorder(span));
                lap_held_.resize(track + 1);
            }
            auto& recorder = lap_takes_[track];
            auto& held = lap_held_[track];
            const auto relative = at - loop.start;
            const bool inside = at >= loop.start && at < loop.end;
            const auto type = event.event.type;
            if (type == blokkily::PluginEvent::Type::note_on && inside) {
                recorder.note_on(relative, key, static_cast<float>(event.event.value), event.event.cents);
                held.push_back({key, lap});
                continue;
            }
            if (type == blokkily::PluginEvent::Type::note_off) {
                if (auto note = recorder.note_off(inside ? relative : span - 1, key)) {
                    // The pass the key went down in owns the note.
                    const auto pressed = std::find_if(held.rbegin(), held.rend(),
                                                      [key](const LapKey& k) { return k.key == key; });
                    std::uint32_t owner = lap;
                    if (pressed != held.rend()) {
                        owner = pressed->lap;
                        held.erase(std::next(pressed).base());
                    }
                    // Held past the wrap: the pass's take ends at the loop's end.
                    note->duration = std::min(note->duration, span - note->start);
                    lap_finished.push_back({track, owner, std::move(*note)});
                    continue;
                }
            }
            if (type == blokkily::PluginEvent::Type::note_expression && inside) {
                const auto dimension = event.event.expression;
                if (dimension >= blokkily::note_dimension::pitch &&
                    dimension <= blokkily::note_dimension::pressure)
                    recorder.expression(relative, key,
                                        static_cast<blokkily::NoteExpression::Kind>(dimension),
                                        static_cast<float>(event.event.value));
                continue;
            }
            if (type == blokkily::PluginEvent::Type::midi_raw && inside) {
                if (auto control = recorder.control(
                        relative, static_cast<std::uint32_t>(event.event.key_or_parameter)))
                    lap_controls.push_back({track, lap, *control});
                continue;
            }
        }
        if (event.event.type == blokkily::PluginEvent::Type::note_on) {
            takes_[track].note_on(at, key, static_cast<float>(event.event.value),
                                  event.event.cents);
        } else if (event.event.type == blokkily::PluginEvent::Type::note_off) {
            if (auto note = takes_[track].note_off(at, key)) finished.emplace_back(track, *note);
        } else if (event.event.type == blokkily::PluginEvent::Type::note_expression) {
            // Per-note expression (MPE) stays with the note it moved.
            const auto dimension = event.event.expression;
            if (dimension >= blokkily::note_dimension::pitch &&
                dimension <= blokkily::note_dimension::pressure)
                takes_[track].expression(at, key,
                                         static_cast<blokkily::NoteExpression::Kind>(dimension),
                                         static_cast<float>(event.event.value));
        } else if (event.event.type == blokkily::PluginEvent::Type::midi_raw) {
            // The wheel, a controller or pressure (wave 4.1).
            if (auto control = takes_[track].control(
                    at, static_cast<std::uint32_t>(event.event.key_or_parameter)))
                controls.emplace_back(track, *control);
        }
    }
    if (!finished.empty() || !controls.empty())
        commitTake(std::move(finished), std::move(controls));
    if (!lap_finished.empty() || !lap_controls.empty())
        commitLapTake(std::move(lap_finished), std::move(lap_controls));
}

void AppController::finishTake() {
    drainTake();
    if (engine_ && song_ != nullptr && engine_->song_samples() > 0) {
        // A key still held is released where the listener had got to, under
        // the clock the render callback was playing: a recompile published
        // but not yet picked up (a tempo drag) must not re-read the position,
        // and the output latency is taken off as it is from every captured
        // key, so a held note keeps the length that was heard.
        const auto at = engine_->heard_tick();
        std::vector<std::pair<std::size_t, blokkily::PlayedNote>> released;
        for (std::size_t track = 0; track < takes_.size(); ++track)
            for (const auto& note : takes_[track].finish(at)) released.emplace_back(track, note);
        if (!released.empty()) commitTake(std::move(released));
        // Keys held in a looped pass end with the take, at the loop's end at
        // the latest.
        const auto& loop = song_->song().loop;
        std::vector<LapNote> lap_released;
        for (std::size_t track = 0; track < lap_takes_.size(); ++track) {
            const auto relative = std::clamp<blokkily::Tick>(at - loop.start, 0, loop.end - loop.start - 1);
            auto& held = lap_held_[track];
            for (auto& note : lap_takes_[track].finish(relative)) {
                const auto pressed = std::find_if(held.begin(), held.end(),
                                                  [&note](const LapKey& k) { return k.key == note.key; });
                const std::uint32_t lap = pressed != held.end() ? pressed->lap : 0;
                if (pressed != held.end()) held.erase(pressed);
                note.duration = std::min(note.duration, loop.end - loop.start - note.start);
                lap_released.push_back({track, lap, std::move(note)});
            }
        }
        if (!lap_released.empty()) commitLapTake(std::move(lap_released), {});
    }
    takes_.clear();
    forgetLapTakes();
    // The audio recorded in the same pass joins the notes' step of history.
    finishAudioTake();
}

void AppController::commitTake(
    std::vector<std::pair<std::size_t, blokkily::PlayedNote>> notes,
    std::vector<std::pair<std::size_t, blokkily::ContinuousEvent>> controls) {
    if (song_ == nullptr || (notes.empty() && controls.empty())) return;
    auto& song = song_->song();
    // One step of history for the whole take: notes and automation alike.
    song_->checkpointTake();
    // Where no clip of the track is under a note, it goes into the track's own
    // part of the open section.
    const auto open = [this](std::size_t track) {
        const int part = song_->partOf(song_->currentSection(), static_cast<int>(track));
        return static_cast<std::size_t>(part >= 0 ? part : std::max(0, song_->currentPattern()));
    };
    for (auto& [track, note] : notes) {
        if (track >= song.tracks.size()) continue;
        const auto target = blokkily::take_target(song, track, note.start, open(track));
        note.start = target.offset;
        (void)blokkily::write_played(song.patterns[target.pattern].pattern, note,
                                     PatternModel::ticks_per_step);
    }
    // Controller movements go where the notes played at the same place go.
    for (auto& [track, control] : controls) {
        if (track >= song.tracks.size()) continue;
        const auto target = blokkily::take_target(song, track, control.tick, open(track));
        control.tick = target.offset;
        blokkily::write_continuous(song.patterns[target.pattern].pattern, control);
    }
    if (pattern_ != nullptr) pattern_->notifyRecorded();
    else requestRecompile();
}

void AppController::forgetLapTakes() {
    lap_takes_.clear();
    lap_held_.clear();
    lap_sections_.clear();
}

// The take section pass `lap` of `track` writes into, made the first time
// the pass plays something: a section of the loop's length, its part on this
// track placed over the loop's range as the take that plays.
std::size_t AppController::lapSection(std::size_t track, std::uint32_t lap) {
    const auto found = lap_sections_.find({track, lap});
    if (found != lap_sections_.end()) return found->second;
    auto& song = song_->song();
    const auto& loop = song.loop;
    // Named after the section played there, and numbered among the takes
    // already over these bars on this track.
    const int open_part = song_->partOf(song_->currentSection(), static_cast<int>(track));
    const auto under = blokkily::take_target(song, track, loop.start,
                                             static_cast<std::size_t>(std::max(0, open_part)));
    std::string base = "TAKE";
    if (under.pattern < song.patterns.size() && song.patterns[under.pattern].section < song.sections.size())
        base = song.sections[song.patterns[under.pattern].section].name;
    int number = 1;
    for (std::size_t clip = 0; clip < song.clips.size(); ++clip) {
        const auto& placed = song.clips[clip];
        if (placed.track == track && blokkily::is_midi_take(song, clip) && placed.start < loop.end &&
            loop.start < placed.start + placed.span(song.patterns[placed.pattern].pattern.length()))
            ++number;
    }
    const auto section = song.add_section(base + " TAKE " + std::to_string(number),
                                          loop.end - loop.start, song.ticks_per_beat());
    song.sections[section].take = true;
    const auto part = song.part(section, track);
    if (part) (void)blokkily::place_midi_take(song, track, *part, loop.start);
    lap_sections_[{track, lap}] = section;
    return section;
}

void AppController::commitLapTake(std::vector<LapNote> notes, std::vector<LapControl> controls) {
    if (song_ == nullptr || (notes.empty() && controls.empty())) return;
    // The same step of history as the rest of the take.
    song_->checkpointTake();
    auto& song = song_->song();
    for (auto& [track, lap, note] : notes) {
        if (track >= song.tracks.size()) continue;
        const auto part = song.part(lapSection(track, lap), track);
        if (!part) continue;
        (void)blokkily::write_played(song.patterns[*part].pattern, note, PatternModel::ticks_per_step);
    }
    for (auto& [track, lap, control] : controls) {
        if (track >= song.tracks.size()) continue;
        const auto part = song.part(lapSection(track, lap), track);
        if (part) blokkily::write_continuous(song.patterns[*part].pattern, control);
    }
    song_->notifyStructureChanged();
}

