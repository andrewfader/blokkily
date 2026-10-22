#include "song_model.hpp"

#include "blokkily/audio/mixer.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <set>

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
    // A step taken back cannot be merged with the next move of a control,
    // and a gesture still open records itself again from here.
    last_merge_.clear();
    gesture_recorded_ = false;
    emit tuningChanged();
    emit mixChanged();
    announceTimebase();
    if (!restored_states.isEmpty()) emit instrumentStatesRestored(restored_states);
    emit processorStatesRestored();
    if (inputs_differ) emit inputChanged();
    notifyStructureChanged();
    emit historyChanged();
    emit notesChanged();
}

void SongModel::beginGesture() {
    gesture_open_ = true;
    gesture_recorded_ = false;
    // A new gesture is a new step of history, however soon it follows the
    // last move of the same control.
    last_merge_.clear();
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
    // The song as it is now is the step being left; the edit about to be
    // made is a new step after it, beside any made after it before and
    // undone, which stay to be gone back to.
    leaveCurrentStep();
    const auto parent = state_id_;
    state_id_ = next_id_++;
    auto& node = history_[state_id_];
    node.parent = parent;
    node.made = QDateTime::currentMSecsSinceEpoch();
    history_[parent].children.push_back(state_id_);
    history_[parent].next = state_id_;
    pruneHistory();
    last_merge_ = merge;
    merge_clock_.restart();
    // A new step for the history to list, whether or not undo or redo
    // became possible.
    emit historyChanged();
}

void SongModel::resetHistory() {
    history_.clear();
    auto& root = history_[state_id_];
    root.made = QDateTime::currentMSecsSinceEpoch();
}

void SongModel::leaveCurrentStep() {
    if (history_.find(state_id_) == history_.end()) resetHistory();
    auto& node = history_[state_id_];
    node.snapshot = snapshot();
    node.stored = true;
}

void SongModel::enterStep(std::uint64_t id) {
    // Redo from anywhere before it leads back along the way it was reached.
    for (auto at = id; history_[at].parent; at = *history_[at].parent)
        history_[*history_[at].parent].next = at;
    auto target = history_[id].snapshot;
    restore(std::move(target));
}

bool SongModel::canUndo() const {
    const auto found = history_.find(state_id_);
    return found != history_.end() && found->second.parent.has_value();
}

bool SongModel::canRedo() const {
    const auto found = history_.find(state_id_);
    return found != history_.end() && found->second.next.has_value();
}

bool SongModel::undo() {
    if (!canUndo()) return false;
    const auto from = state_id_;
    const auto to = *history_[from].parent;
    leaveCurrentStep();
    history_[to].next = from;
    enterStep(to);
    return true;
}

bool SongModel::redo() {
    if (!canRedo()) return false;
    const auto to = *history_[state_id_].next;
    leaveCurrentStep();
    enterStep(to);
    return true;
}

bool SongModel::jumpToHistory(qint64 id) {
    const auto target = static_cast<std::uint64_t>(id);
    const auto found = history_.find(target);
    if (found == history_.end() || target == state_id_) return found != history_.end();
    leaveCurrentStep();
    if (!history_[target].stored) return false;
    enterStep(target);
    return true;
}

void SongModel::pruneHistory() {
    if (history_.size() <= history_limit) return;
    // The line from the start to where the song is now is kept longest.
    std::set<std::uint64_t> line;
    for (std::optional<std::uint64_t> at = state_id_; at; at = history_[*at].parent) line.insert(*at);
    while (history_.size() > history_limit) {
        // The oldest step off that line with nothing made after it goes first.
        std::optional<std::uint64_t> oldest;
        for (const auto& [id, node] : history_)
            if (!line.contains(id) && node.children.empty() &&
                (!oldest || node.made < history_[*oldest].made))
                oldest = id;
        if (oldest) {
            const auto parent = history_[*oldest].parent;
            if (parent) {
                auto& siblings = history_[*parent].children;
                std::erase(siblings, *oldest);
                if (history_[*parent].next == oldest) history_[*parent].next.reset();
            }
            history_.erase(*oldest);
            continue;
        }
        // Only the line is left: its first step goes, and what follows it on
        // the line starts history.
        const auto first = *std::find_if(line.begin(), line.end(),
                                         [this](auto id) { return !history_[id].parent; });
        const auto& children = history_[first].children;
        const auto after = std::find_if(children.begin(), children.end(),
                                        [&line](auto id) { return line.contains(id); });
        if (after == children.end()) break;
        const auto successor = *after;
        // Its other branches go with it.
        std::vector<std::uint64_t> doomed{first};
        for (const auto child : children)
            if (child != successor) doomed.push_back(child);
        for (std::size_t index = 0; index < doomed.size(); ++index) {
            const auto id = doomed[index];
            if (id != first)
                for (const auto child : history_[id].children) doomed.push_back(child);
        }
        for (const auto id : doomed) history_.erase(id);
        line.erase(first);
        history_[successor].parent.reset();
    }
}

namespace {
// What one step changed, said in a few words, read from the song before it
// and after it.
QString describe_step(const blokkily::Song& before, const blokkily::Song& after) {
    const auto plural = [](std::size_t count, const char* one) {
        return count == 1 ? QString::fromLatin1(one) : QStringLiteral("%1 %2s").arg(count).arg(one);
    };
    if (after.tracks.size() != before.tracks.size())
        return after.tracks.size() > before.tracks.size() ? QStringLiteral("Track added")
                                                          : QStringLiteral("Track deleted");
    for (std::size_t index = 0; index < after.tracks.size(); ++index)
        if (after.tracks[index].name != before.tracks[index].name) {
            bool moved = false;
            for (const auto& track : before.tracks) moved = moved || track.name == after.tracks[index].name;
            return moved ? QStringLiteral("Tracks reordered")
                         : QStringLiteral("Track renamed %1").arg(QString::fromStdString(after.tracks[index].name));
        }
    if (after.sections.size() != before.sections.size())
        return after.sections.size() > before.sections.size() ? QStringLiteral("Pattern added")
                                                              : QStringLiteral("Pattern deleted");
    std::size_t notes_before = 0, notes_after = 0;
    for (const auto& slot : before.patterns) notes_before += slot.pattern.events().size();
    for (const auto& slot : after.patterns) notes_after += slot.pattern.events().size();
    if (notes_after > notes_before) return plural(notes_after - notes_before, "step") + QStringLiteral(" written");
    if (notes_after < notes_before) return plural(notes_before - notes_after, "step") + QStringLiteral(" cleared");
    if (after.clips.size() != before.clips.size())
        return after.clips.size() > before.clips.size() ? QStringLiteral("Clip placed")
                                                        : QStringLiteral("Clip removed");
    for (std::size_t index = 0; index < after.clips.size(); ++index)
        if (after.clips[index].start != before.clips[index].start ||
            after.clips[index].repeats != before.clips[index].repeats ||
            after.clips[index].track != before.clips[index].track)
            return QStringLiteral("Clip moved");
    if (after.audio_clips != before.audio_clips) return QStringLiteral("Audio clip edited");
    for (std::size_t index = 0; index < after.tracks.size(); ++index) {
        const auto name = QString::fromStdString(after.tracks[index].name);
        const auto& was = before.tracks[index];
        const auto& now = after.tracks[index];
        if (!(was.instrument.format == now.instrument.format && was.instrument.path == now.instrument.path &&
              was.instrument.identifier == now.instrument.identifier))
            return QStringLiteral("%1 instrument").arg(name);
        if (was.inserts.size() != now.inserts.size())
            return QStringLiteral("%1 effect %2").arg(name, now.inserts.size() > was.inserts.size()
                                                                ? QStringLiteral("added") : QStringLiteral("removed"));
        if (!(was.mix == now.mix)) return QStringLiteral("%1 mix").arg(name);
        if (was.colour != now.colour) return QStringLiteral("%1 colour").arg(name);
        if (!(was.automation == now.automation)) return QStringLiteral("%1 automation").arg(name);
    }
    if (!(after.tempo == before.tempo)) return QStringLiteral("Tempo");
    if (!(after.meter == before.meter)) return QStringLiteral("Meter");
    if (after.master_gain_db != before.master_gain_db) return QStringLiteral("Master level");
    if (after.returns != before.returns) return QStringLiteral("Returns");
    return QStringLiteral("Notes edited");
}
} // namespace

QVariantList SongModel::history() const {
    QVariantList list;
    if (history_.empty()) return list;
    std::set<std::uint64_t> line;
    for (auto found = history_.find(state_id_); found != history_.end();) {
        line.insert(found->first);
        if (!found->second.parent) break;
        found = history_.find(*found->second.parent);
    }
    const auto song_at = [this](std::uint64_t id) -> const blokkily::Song& {
        const auto& node = history_.at(id);
        return id == state_id_ ? song_ : node.snapshot.song;
    };
    // Depth first from the start, a step's later branches indented under it.
    std::uint64_t root = history_.begin()->first;
    for (const auto& [id, node] : history_)
        if (!node.parent) root = id;
    std::vector<std::pair<std::uint64_t, int>> pending{{root, 0}};
    while (!pending.empty()) {
        const auto [id, depth] = pending.back();
        pending.pop_back();
        const auto& node = history_.at(id);
        QString label = QStringLiteral("Start");
        if (node.parent) label = describe_step(song_at(*node.parent), song_at(id));
        list.push_back(QVariantMap{{"id", static_cast<qint64>(id)},
                                   {"label", label},
                                   {"time", QDateTime::fromMSecsSinceEpoch(node.made).toString("HH:mm:ss")},
                                   {"depth", depth},
                                   {"current", id == state_id_},
                                   {"onPath", line.contains(id)}});
        // The first child continues the line; later ones are branches.
        for (std::size_t index = node.children.size(); index-- > 0;)
            pending.push_back({node.children[index], depth + (index == 0 ? 0 : 1)});
    }
    return list;
}

void SongModel::markSaved() {
    saved_id_ = state_id_;
    versions_changed_ = false;
    last_merge_.clear();
    emit historyChanged();
}

void SongModel::markUnsaved() {
    // An id no state ever has.
    saved_id_ = next_id_++;
    emit historyChanged();
}

void SongModel::clearHistory() {
    state_id_ = next_id_++;
    resetHistory();
    last_merge_.clear();
    emit historyChanged();
}
