#include "app_controller.hpp"
#include "blokkily/instruments/soundfont_catalog.hpp"
#include "blokkily/instruments/soundfont_synth.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/playback.hpp"
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

void AppController::drainTake() {
    if (!engine_ || song_ == nullptr || engine_->sample_rate() <= 0.0) return;
    // Every sample the engine stamped is read back through the song's tempo
    // map, the one sample-to-tick path, so a take played across a tempo change
    // lands on the ticks that were heard.
    const auto& clock = engine_->published_clock();
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
        const auto at = blokkily::tick_at_sample(clock, event.sample);
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
