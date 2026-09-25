#include "app_controller.hpp"
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

void AppController::refreshMidiPorts() {
    QStringList ports;
    if (midi_input_->mode() == blokkily::MidiInput::Mode::deterministic) {
        ports << QStringLiteral("Deterministic input");
    } else {
        for (const auto& name : midi_input_->ports()) ports << QString::fromStdString(name);
    }
    if (ports == midi_ports_) return;
    midi_ports_ = ports;
    emit midiChanged();
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
    take_checkpointed_ = false;
    if (engine_) engine_->set_recording(record_armed_);
    status_ = record_armed_ ? QStringLiteral("Recording armed · play to record")
                            : QStringLiteral("Recording off");
    emit statusChanged();
    emit recordChanged();
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
    for (const auto& event : heard) {
        const std::size_t track = event.track;
        if (takes_.size() <= track) takes_.resize(track + 1, blokkily::TakeRecorder(length));
        const auto at = event.tick;
        const auto key = static_cast<std::int16_t>(event.event.key_or_parameter);
        if (event.event.type == blokkily::PluginEvent::Type::note_on) {
            takes_[track].note_on(at, key, static_cast<float>(event.event.value),
                                  event.event.cents);
        } else if (event.event.type == blokkily::PluginEvent::Type::note_off) {
            if (auto note = takes_[track].note_off(at, key)) finished.emplace_back(track, *note);
        }
    }
    if (!finished.empty()) commitTake(std::move(finished));
}

void AppController::finishTake() {
    drainTake();
    if (engine_ && song_ != nullptr && engine_->song_samples() > 0) {
        const auto at = blokkily::tick_at_sample(
            engine_->published_clock(), engine_->sample_position() % engine_->song_samples());
        std::vector<std::pair<std::size_t, blokkily::PlayedNote>> released;
        for (std::size_t track = 0; track < takes_.size(); ++track)
            for (const auto& note : takes_[track].finish(at)) released.emplace_back(track, note);
        if (!released.empty()) commitTake(std::move(released));
    }
    takes_.clear();
}

void AppController::commitTake(std::vector<std::pair<std::size_t, blokkily::PlayedNote>> notes) {
    if (song_ == nullptr || notes.empty()) return;
    auto& song = song_->song();
    if (!take_checkpointed_) {
        song_->checkpoint();
        take_checkpointed_ = true;
    }
    const auto open = static_cast<std::size_t>(std::max(0, song_->currentPattern()));
    for (auto& [track, note] : notes) {
        if (track >= song.tracks.size()) continue;
        const auto target = blokkily::take_target(song, track, note.start, open);
        note.start = target.offset;
        (void)blokkily::write_played(song.patterns[target.pattern].pattern, note,
                                     PatternModel::ticks_per_step);
    }
    if (pattern_ != nullptr) pattern_->notifyRecorded();
    else requestRecompile();
}
