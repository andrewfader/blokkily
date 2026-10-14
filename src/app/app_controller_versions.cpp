// Saved versions of the song (features/versions.feature). A version is the
// project file's text for the song as it was, so restoring one is opening it
// into the session without losing the session's history.

#include "app_controller.hpp"

#include <QDateTime>
#include <QFileInfo>

namespace {
std::filesystem::path base_dir_of(const QString& project_path) {
    if (project_path.isEmpty()) return {};
    return std::filesystem::path(project_path.toStdString()).parent_path();
}
} // namespace

bool AppController::saveVersion(const QString& name) {
    if (song_ == nullptr) return false;
    auto& song = song_->song();
    // What each processor sounds like now goes into the version with it.
    if (engine_) (void)blokkily::capture_processor_states(*engine_, song, engine_signature_);
    blokkily::Project project;
    project.name = "Blokkily Session";
    project.song = song;
    const auto label = name.trimmed().isEmpty()
                           ? QString("VERSION %1").arg(song_->versions().size() + 1)
                           : name.trimmed();
    song_->addVersion({label.toStdString(), QDateTime::currentSecsSinceEpoch(),
                       blokkily::ProjectFile::serialize(project, base_dir_of(project_path_))});
    status_ = QString("Saved version %1").arg(label);
    emit statusChanged();
    return true;
}

bool AppController::restoreVersion(int index) {
    if (song_ == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= song_->versions().size())
        return false;
    // A copy: saving the song as it is now, below, may move the list.
    const auto version = song_->versions()[static_cast<std::size_t>(index)];
    std::string error;
    auto project = blokkily::ProjectFile::parse(version.snapshot, &error, base_dir_of(project_path_));
    if (!project) {
        status_ = QString("Version %1 could not be read · %2")
                      .arg(QString::fromStdString(version.name), QString::fromStdString(error));
        emit statusChanged();
        return false;
    }
    if (transport_ != nullptr) transport_->stop();
    forgetSoundingNotes();
    takes_.clear();
    discardAudioTake();
    // What every processor sounds like now goes with the song being replaced,
    // into the step of history the restore makes and into the version below.
    if (engine_)
        (void)blokkily::capture_processor_states(*engine_, song_->song(), engine_signature_);
    // The song being replaced is kept as a version of its own unless it
    // already is one, so restoring never loses work, even after the session
    // (and its undo) is gone.
    const auto label = QString::fromStdString(version.name);
    blokkily::Project now;
    now.name = "Blokkily Session";
    now.song = song_->song();
    const auto current = blokkily::ProjectFile::serialize(now, base_dir_of(project_path_));
    const bool kept = !song_->songIsAVersion(current);
    if (kept)
        song_->addVersion({QString("BEFORE %1").arg(label).toStdString(),
                           QDateTime::currentSecsSinceEpoch(), current});
    song_->restoreSong(std::move(project->song));
    if (pattern_ != nullptr) pattern_->refresh();
    if (transport_ != nullptr) transport_->rewind();
    status_ = kept ? QString("Restored %1 · the song before it is saved as BEFORE %1").arg(label)
                   : QString("Restored %1 · UNDO brings back the song before it").arg(label);
    emit statusChanged();
    return true;
}

bool AppController::renameVersion(int index, const QString& name) {
    return song_ != nullptr && song_->renameVersion(index, name);
}

bool AppController::deleteVersion(int index) {
    if (song_ == nullptr || index < 0 ||
        static_cast<std::size_t>(index) >= song_->versions().size())
        return false;
    const auto label = QString::fromStdString(song_->versions()[static_cast<std::size_t>(index)].name);
    if (!song_->removeVersion(index)) return false;
    status_ = QString("Deleted version %1").arg(label);
    emit statusChanged();
    return true;
}
