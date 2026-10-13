// The song's notes and the project's saved versions (features/versions.feature).

#include "song_model.hpp"

#include <QDateTime>

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
// What a version holds, said briefly: its sections, tracks and tempo.
QString summary_of(const blokkily::ProjectVersion& version) {
    std::string error;
    const auto project = blokkily::ProjectFile::parse(version.snapshot, &error);
    if (!project) return QStringLiteral("unreadable");
    const auto& song = project->song;
    const auto sections = song.sections.empty() ? song.patterns.size() : song.sections.size();
    const auto tracks = song.tracks.size();
    const double bpm = song.tempo.points.empty() ? 120.0 : song.tempo.points.front().bpm;
    return QString("%1 pattern%2 · %3 track%4 · %5 BPM")
        .arg(sections).arg(sections == 1 ? "" : "s")
        .arg(tracks).arg(tracks == 1 ? "" : "s")
        .arg(bpm, 0, 'f', bpm == static_cast<int>(bpm) ? 0 : 2);
}
} // namespace

QVariantList SongModel::versionRows() const {
    QVariantList rows;
    for (int index = static_cast<int>(versions_.size()) - 1; index >= 0; --index) {
        const auto& version = versions_[static_cast<std::size_t>(index)];
        rows.push_back(QVariantMap{
            {"index", index},
            {"name", QString::fromStdString(version.name)},
            {"when", QDateTime::fromSecsSinceEpoch(version.saved_at)
                         .toLocalTime()
                         .toString(QStringLiteral("yyyy-MM-dd HH:mm"))},
            {"summary", summary_of(version)}});
    }
    return rows;
}

void SongModel::setVersions(std::vector<blokkily::ProjectVersion> versions) {
    versions_ = std::move(versions);
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
