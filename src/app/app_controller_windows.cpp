// More than one window at once (features/multiple_windows.feature). Each
// window is a Blokkily process of its own, with its own song, engine, audio
// stream and autosave, so one window's plugin crashing or song closing never
// touches another's.

#include "app_controller.hpp"
#include "open_channel.hpp"

#include <QCoreApplication>
#include <QFileInfo>
#include <QUrl>

namespace {
QString absolute_local(const QString& value) {
    const QUrl url(value);
    return QFileInfo(url.isLocalFile() ? url.toLocalFile() : value).absoluteFilePath();
}
} // namespace

bool AppController::holdsFile(const QString& path) const {
    return !project_path_.isEmpty() &&
           QFileInfo(absolute_local(path)).canonicalFilePath() ==
               QFileInfo(project_path_).canonicalFilePath();
}

bool AppController::forwardToHolder(const QString& path) {
    // A window holding the file listens on a channel named for it; handed
    // the file there, it comes forward. Nobody listening answers at once.
    if (!open_channel::forward(open_channel::fileSocketPath(path), absolute_local(path), 3000))
        return false;
    status_ = QStringLiteral("%1 is open in another window")
                  .arg(QFileInfo(absolute_local(path)).completeBaseName());
    emit statusChanged();
    return true;
}

bool AppController::openWindow(const QString& path) {
    if (!path.isEmpty()) {
        if (holdsFile(path)) {
            emit raiseRequested();
            return true;
        }
        if (forwardToHolder(path)) return true;
    }
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
    if (holdsFile(path)) return true;
    // Open in another window already: that one comes forward instead.
    if (forwardToHolder(path)) return false;
    if (project_path_.isEmpty() && (song_ == nullptr || !song_->dirty())) {
        emit openRequested(requested.absoluteFilePath());
        return true;
    }
    (void)openWindow(requested.absoluteFilePath());
    return false;
}
