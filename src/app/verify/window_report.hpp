#pragma once

// What a window a gate opened reports back (features/multiple_windows.feature).
// The gate's windows are real Blokkily processes started as the interface
// starts them, with the gate's arguments added (scenario "opened_window"):
// each writes what it holds beside its screenshot and exits.

#include "verify/harness.hpp"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QString>
#include <QStringList>

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

inline void forget_window_report(const QString& screenshot) {
    QFile::remove(window_report_path(screenshot));
    QFile::remove(screenshot);
}

} // namespace blokkily::verify
