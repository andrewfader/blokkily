#pragma once

// What a window a gate opened reports back (features/multiple_windows.feature).
// The gate's windows are real Blokkily processes started as the interface
// starts them, with the gate's arguments added (scenario "opened_window"):
// each writes what it holds beside its screenshot, then stays open, holding
// its song's channel, until the gate stops it. One asked to come forward
// meanwhile leaves a mark beside its screenshot too.

#include "verify/harness.hpp"
#include "mcp_protocol.hpp"
#include "open_channel.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>

namespace blokkily::verify {

struct WindowReport {
    QString project;
    QString first_track;
    QString title;
    bool dirty = true;
    bool passed = false;
};

inline QString window_report_path(const QString& screenshot) { return screenshot + ".ini"; }

// The arguments that make a window this gate opens report to `screenshot`.
inline QStringList window_report_arguments(const QString& screenshot) {
    return {QStringLiteral("--verify"), QStringLiteral("--scenario"),
            QStringLiteral("opened_window"), QStringLiteral("--screenshot"), screenshot};
}

// The report of the window started since the last forget_window_report, once
// it has written one; nothing after `ms`.
inline std::optional<WindowReport> await_window_report(const QString& screenshot, int ms) {
    const auto path = window_report_path(screenshot);
    QElapsedTimer clock;
    clock.start();
    while (!QFileInfo::exists(path) && clock.elapsed() < ms) VerifyContext::settle(50);
    if (!QFileInfo::exists(path)) return std::nullopt;
    const QSettings ini(path, QSettings::IniFormat);
    return WindowReport{ini.value("project").toString(), ini.value("first_track").toString(),
                        ini.value("title").toString(), ini.value("dirty", true).toBool(),
                        ini.value("passed").toBool()};
}

inline QString window_stop_path(const QString& screenshot) { return screenshot + ".stop"; }
inline QString window_raised_path(const QString& screenshot) { return screenshot + ".raised"; }

inline void touch_file(const QString& path) {
    QFile file(path);
    (void)file.open(QIODevice::WriteOnly);
}

inline void forget_window_report(const QString& screenshot) {
    for (const auto& path : {window_report_path(screenshot), window_stop_path(screenshot),
                             window_raised_path(screenshot), screenshot})
        QFile::remove(path);
}

inline bool wait_until(const std::function<bool()>& ready, int ms) {
    QElapsedTimer clock;
    clock.start();
    while (!ready() && clock.elapsed() < ms) VerifyContext::settle(20);
    return ready();
}

// Whether the window reporting to `screenshot` was asked to come forward.
inline bool await_window_raised(const QString& screenshot, int ms) {
    return wait_until([&] { return QFileInfo::exists(window_raised_path(screenshot)); }, ms);
}

// Closes the window reporting to `screenshot`, waiting until no window holds
// `file` any more (when one is named).
inline bool stop_window(const QString& screenshot, const QString& file = QString()) {
    touch_file(window_stop_path(screenshot));
    if (file.isEmpty()) return true;
    return wait_until([&] {
        return !blokkily::mcp::SocketLock::taken(open_channel::fileSocketPath(file));
    }, 15000);
}

} // namespace blokkily::verify
