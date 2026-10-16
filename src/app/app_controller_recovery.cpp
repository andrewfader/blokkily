// Autosave and crash recovery (features/autosave.feature): an unsaved song is
// copied to the recovery folder while it has changes, and a song a window
// left there when it did not close normally is offered back at the next
// launch.

#include "app_controller.hpp"

#include <QFileInfo>
#include <QVariantMap>

namespace {

// The folder a project's relative paths are taken against: its own, or none
// for a song never saved (whose files are then named in full).
std::filesystem::path base_of(const QString& project) {
    return project.isEmpty() ? std::filesystem::path{}
                             : std::filesystem::path(QFileInfo(project).absolutePath().toStdString());
}

} // namespace

void AppController::startAutosave(const QString& directory, int interval_ms) {
    if (recovery_) return; // once per window
    recovery_ = std::make_unique<RecoveryStore>(directory);
    if (!recovery_->enabled()) return;
    (void)recovery_->own_id();
    autosave_timer_.setInterval(std::max(100, interval_ms));
    connect(&autosave_timer_, &QTimer::timeout, this, [this] {
        // Never in the middle of a load or an export: the song is between
        // two states then, and the engine is busy.
        if (progress_.loading()) return;
        (void)autosaveNow();
    });
    autosave_timer_.start();
    emit recoveriesChanged();
}

QVariantList AppController::recoveries() const {
    QVariantList list;
    if (!recovery_) return list;
    for (const auto& entry : recovery_->abandoned())
        list.append(QVariantMap{{"id", entry.id},
                                {"project", entry.project},
                                {"title", entry.title},
                                {"saved", entry.saved.toLocalTime().toString("ddd d MMM HH:mm")}});
    return list;
}

bool AppController::autosaveNow() {
    if (!recovery_ || !recovery_->enabled() || song_ == nullptr) return false;
    if (!song_->dirty()) {
        forgetAutosave();
        return true;
    }
    // What each live processor holds now is part of the song, as a save
    // asks for it.
    auto& song = song_->song();
    if (engine_) (void)blokkily::capture_processor_states(*engine_, song, engine_signature_);
    blokkily::Project project;
    project.name = "Blokkily Session";
    project.song = song;
    project.versions = song_->versions();
    // Written against the project's own folder, so it reads back exactly as
    // the project would; recordings are not moved, as a save would move them.
    const auto text = QByteArray::fromStdString(
        blokkily::ProjectFile::serialize(project, base_of(project_path_)));
    if (text == last_autosave_ && recovery_->has_own_entry()) return true;
    QString error;
    const bool written = recovery_->write(text, project_path_, projectName(), &error);
    if (written) last_autosave_ = text;
    autosave_status_ = written ? QStringLiteral("Autosaved %1").arg(QTime::currentTime().toString("HH:mm"))
                               : QStringLiteral("Autosave failed · %1").arg(error);
    emit autosaveStatusChanged();
    return written;
}

void AppController::forgetAutosave() {
    last_autosave_.clear();
    if (recovery_) recovery_->clear();
    if (!autosave_status_.isEmpty()) {
        autosave_status_.clear();
        emit autosaveStatusChanged();
    }
}

bool AppController::recoverSession(const QString& id) {
    if (!recovery_ || song_ == nullptr) return false;
    QString project_path;
    for (const auto& entry : recovery_->abandoned())
        if (entry.id == id) project_path = entry.project;
    const auto text = recovery_->read(id);
    if (!text) return false;
    std::string error;
    auto project = blokkily::ProjectFile::parse(text->toStdString(), &error, base_of(project_path));
    if (!project) {
        project_status_ = "Recovery failed";
        project_detail_ = QString::fromStdString(error);
        emit projectStatusChanged();
        return false;
    }
    {
        const LoadProgress::Scope opening(progress_, QStringLiteral("RECOVERING UNSAVED WORK"));
        installProject(std::move(*project), project_path);
    }
    if (transport_) transport_->rewind();
    // It is the song as it was, not as it is on disk: unsaved until saved.
    song_->markUnsaved();
    project_status_ = QStringLiteral("Recovered unsaved work");
    emit projectStatusChanged();
    (void)recovery_->discard(id);
    // This window keeps it safe from here on.
    (void)autosaveNow();
    emit recoveriesChanged();
    return true;
}

bool AppController::discardRecovery(const QString& id) {
    if (!recovery_) return false;
    const bool discarded = recovery_->discard(id);
    emit recoveriesChanged();
    return discarded;
}
