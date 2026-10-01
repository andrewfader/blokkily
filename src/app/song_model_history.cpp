#include "song_model.hpp"

#include "blokkily/audio/mixer.hpp"

#include <QFileInfo>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace {
// How far back history reaches. Each step holds a whole song, which is small
// next to a minute of audio, so a generous limit costs little.
constexpr std::size_t history_limit = 200;
// Moves of one control closer together than this are one gesture.
constexpr qint64 merge_window_ms = 1200;
} // namespace

SongModel::Snapshot SongModel::snapshot() const {
    return {song_, current_section_, selected_track_, state_id_};
}

void SongModel::restore(Snapshot snapshot) {
    // Tracks that keep their instrument but get an earlier state of it back.
    QList<int> restored_states;
    const auto& before = song_.tracks;
    const auto& after = snapshot.song.tracks;
    for (std::size_t track = 0; track < std::min(before.size(), after.size()); ++track) {
        const auto& was = before[track].instrument;
        const auto& now = after[track].instrument;
        if (!now.format.empty() && was.format == now.format && was.path == now.path &&
            was.identifier == now.identifier && was.state != now.state)
            restored_states.push_back(static_cast<int>(track));
    }
    // Arm and input are how the session is wired, not what it plays: a step
    // of history keeps them as they are now (decision 5).
    keepInputs(song_, snapshot.song);
    // The click and the count-in are session settings too (item 3.7).
    snapshot.song.metronome = song_.metronome;
    // Only a step that brings a track back or takes one away can change which
    // tracks are armed.
    const bool inputs_differ = [&] {
        if (snapshot.song.tracks.size() != song_.tracks.size()) return true;
        for (std::size_t index = 0; index < song_.tracks.size(); ++index)
            if (!(snapshot.song.tracks[index].input == song_.tracks[index].input)) return true;
        return false;
    }();
    song_ = std::move(snapshot.song);
    current_section_ = qBound(0, snapshot.current_section,
                              std::max(0, static_cast<int>(song_.sections.size()) - 1));
    selected_track_ = qBound(0, snapshot.selected_track,
                             static_cast<int>(song_.tracks.size()) - 1);
    state_id_ = snapshot.id;
    peaks_.assign(song_.tracks.size(), 0.0F);
    // A step taken back cannot be merged with the next move of a control.
    last_merge_.clear();
    emit tuningChanged();
    emit mixChanged();
    emit timebaseChanged();
    if (!restored_states.isEmpty()) emit instrumentStatesRestored(restored_states);
    emit processorStatesRestored();
    if (inputs_differ) emit inputChanged();
    notifyStructureChanged();
    emit historyChanged();
}

void SongModel::beginGesture() {
    gesture_open_ = true;
    gesture_recorded_ = false;
}

void SongModel::endGesture() {
    gesture_open_ = false;
    gesture_recorded_ = false;
}

void SongModel::checkpoint(const QString& merge) {
    if (gesture_open_) {
        if (gesture_recorded_) return;
        gesture_recorded_ = true;
    }
    if (!merge.isEmpty() && merge == last_merge_ && merge_clock_.isValid() &&
        merge_clock_.elapsed() < merge_window_ms) {
        merge_clock_.restart();
        return;
    }
    const bool was_dirty = dirty();
    const bool could_undo = canUndo();
    const bool could_redo = canRedo();
    undo_.push_back(snapshot());
    if (undo_.size() > history_limit) undo_.erase(undo_.begin());
    redo_.clear();
    state_id_ = next_id_++;
    last_merge_ = merge;
    merge_clock_.restart();
    if (was_dirty != dirty() || could_undo != canUndo() || could_redo != canRedo())
        emit historyChanged();
}

bool SongModel::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(snapshot());
    auto previous = std::move(undo_.back());
    undo_.pop_back();
    restore(std::move(previous));
    return true;
}

bool SongModel::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(snapshot());
    auto next = std::move(redo_.back());
    redo_.pop_back();
    restore(std::move(next));
    return true;
}

void SongModel::markSaved() {
    saved_id_ = state_id_;
    last_merge_.clear();
    emit historyChanged();
}

void SongModel::clearHistory() {
    undo_.clear();
    redo_.clear();
    state_id_ = next_id_++;
    last_merge_.clear();
    emit historyChanged();
}
