// More than one window at once (features/multiple_windows.feature). Each
// window is a Blokkily process of its own, with its own song, engine, audio
// stream and autosave, so one window's plugin crashing or song closing never
// touches another's.

#include "app_controller.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QUrl>

namespace {
QString absolute_local(const QString& value) {
    const QUrl url(value);
    return QFileInfo(url.isLocalFile() ? url.toLocalFile() : value).absoluteFilePath();
}
} // namespace

bool AppController::openWindow(const QString& path) {
    // --new-window keeps the new process from handing its file straight back
    // to the window that is open (open_channel.hpp).
    QStringList arguments{QStringLiteral("--new-window")};
    arguments += window_arguments_;
    if (!path.isEmpty()) arguments << absolute_local(path);
    const bool started =
        QProcess::startDetached(QCoreApplication::applicationFilePath(), arguments);
    status_ = !started ? QStringLiteral("Could not open a new window")
              : path.isEmpty()
                  ? QStringLiteral("Opened a new window")
                  : QStringLiteral("Opened %1 in a new window")
                        .arg(QFileInfo(absolute_local(path)).completeBaseName());
    emit statusChanged();
    return started;
}

bool AppController::routeOpenRequest(const QString& path) {
    const QFileInfo requested(absolute_local(path));
    if (!project_path_.isEmpty() &&
        requested.canonicalFilePath() == QFileInfo(project_path_).canonicalFilePath())
        return true;
    if (project_path_.isEmpty() && (song_ == nullptr || !song_->dirty())) {
        emit openRequested(requested.absoluteFilePath());
        return true;
    }
    (void)openWindow(requested.absoluteFilePath());
    return false;
}
