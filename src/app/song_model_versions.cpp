// The song's notes and the project's saved versions (features/versions.feature).

#include "song_model.hpp"

#include <QDateTime>

#include <algorithm>

QString SongModel::notes() const { return QString::fromStdString(song_.notes); }

void SongModel::setNotes(const QString& text) {
    const auto wanted = text.toStdString();
    if (wanted == song_.notes) return;
    // A run of typing is one step of history, as a fader's travel is.
    checkpoint(QStringLiteral("notes"));
    song_.notes = wanted;
    emit notesChanged();
}

namespace {
// The figures a version is told apart by: its tempo, tracks, patterns (what a
// producer calls a pattern: a section) and how many notes it plays.
QVariantMap figures_of(const blokkily::Song& song) {
    const auto patterns = song.sections.empty() ? song.patterns.size() : song.sections.size();
    std::size_t notes = 0;
    for (const auto& slot : song.patterns) notes += slot.pattern.events().size();
    return {{"bpm", song.tempo.points.empty() ? 120.0 : song.tempo.points.front().bpm},
            {"tracks", static_cast<int>(song.tracks.size())},
            {"patterns", static_cast<int>(patterns)},
            {"notes", static_cast<int>(notes)},
            {"readable", true}};
}
} // namespace

QVariantMap SongModel::songFigures() const { return figures_of(song_); }

bool SongModel::songIsAVersion(const std::string& snapshot) const {
    return std::any_of(versions_.begin(), versions_.end(),
                       [&](const blokkily::ProjectVersion& version) {
                           return version.snapshot == snapshot;
                       });
}

QVariantList SongModel::versionRows() const {
    if (version_figures_.size() != versions_.size()) {
        version_figures_.clear();
        for (const auto& version : versions_) {
            std::string error;
            const auto project = blokkily::ProjectFile::parse(version.snapshot, &error);
            version_figures_.push_back(project ? figures_of(project->song)
                                               : QVariantMap{{"readable", false}});
        }
    }
    QVariantList rows;
    for (int index = static_cast<int>(versions_.size()) - 1; index >= 0; --index) {
        const auto& version = versions_[static_cast<std::size_t>(index)];
        auto row = version_figures_[static_cast<std::size_t>(index)];
        row["index"] = index;
        row["name"] = QString::fromStdString(version.name);
        row["when"] = QDateTime::fromSecsSinceEpoch(version.saved_at)
                          .toLocalTime()
                          .toString(QStringLiteral("yyyy-MM-dd HH:mm"));
        rows.push_back(row);
    }
    return rows;
}

void SongModel::setVersions(std::vector<blokkily::ProjectVersion> versions) {
    versions_ = std::move(versions);
    version_figures_.clear();
    versions_changed_ = false;
    emit versionsChanged();
    emit historyChanged();
}

void SongModel::addVersion(blokkily::ProjectVersion version) {
    versions_.push_back(std::move(version));
    versions_changed_ = true;
    emit versionsChanged();
    emit historyChanged();
}

bool SongModel::removeVersion(int index) {
    if (index < 0 || static_cast<std::size_t>(index) >= versions_.size()) return false;
    versions_.erase(versions_.begin() + index);
    version_figures_.clear();
    versions_changed_ = true;
    emit versionsChanged();
    emit historyChanged();
    return true;
}

bool SongModel::renameVersion(int index, const QString& name) {
    if (index < 0 || static_cast<std::size_t>(index) >= versions_.size() || name.trimmed().isEmpty())
        return false;
    versions_[static_cast<std::size_t>(index)].name = name.trimmed().toStdString();
    versions_changed_ = true;
    emit versionsChanged();
    emit historyChanged();
    return true;
}

void SongModel::restoreSong(blokkily::Song song) {
    checkpoint();
    // The studio's calibration is the studio's, whatever song it plays.
    song.record_offset_samples = song_.record_offset_samples;
    install(std::move(song));
}
