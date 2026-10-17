// features/open_in_running_window.feature, run by bdd_open_running: a second
// `blokkily song.blok`, started as the desktop starts it, hands the file to
// this window and exits. A window that holds a song keeps it and opens the
// file in a new window; one that holds nothing yet opens the file itself.
#include "verify/harness.hpp"
#include "verify/window_report.hpp"
#include "song_model.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QUrl>

#include <functional>
#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

bool wait_for(const std::function<bool()>& ready, int ms) {
    QElapsedTimer clock;
    clock.start();
    while (!ready() && clock.elapsed() < ms) VerifyContext::settle(20);
    return ready();
}

void run_open_running(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "open running failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const QString socket = qEnvironmentVariable("BLOKKILY_OPEN_SOCKET");
    check(!socket.isEmpty() && QFileInfo::exists(socket));
    const QFileInfo project(ctx.parser.value("project"));
    check(project.exists());
    // A window this one opens for a file is a real process that reports
    // what it holds (verify/window_report.hpp).
    const QString artifacts = QFileInfo(ctx.parser.value("screenshot")).path();
    const QString child_png = artifacts + "/open-running-window.png";
    const QString url_png = artifacts + "/open-running-url-window.png";
    for (const auto& png : {child_png, url_png}) forget_window_report(png);
    c.setWindowArgumentsForTesting(window_report_arguments(child_png));
    auto* dialog = ctx.window->findChild<QObject*>("discardDialog");
    check(dialog != nullptr);
    const auto asked = [&] { return dialog != nullptr && dialog->property("opened").toBool(); };

    // The desktop's launch: the real executable, with the file named
    // relative to the folder it runs in, and nothing to tell it a window is
    // open but the channel.
    QProcess second;
    second.setProgram(QCoreApplication::applicationFilePath());
    second.setWorkingDirectory(project.absolutePath());
    auto environment = QProcessEnvironment::systemEnvironment();
    // Were the hand-over to fail it would open a window of its own; keep that
    // window's autosave out of the user's folder.
    environment.insert("BLOKKILY_RECOVERY_DIR", QDir(project.absolutePath()).filePath("second-recovery"));
    environment.insert("BLOKKILY_MCP", "0");
    second.setProcessEnvironment(environment);
    const auto launch = [&](const QString& argument) {
        second.setArguments({argument});
        second.start();
        check(second.waitForStarted(5000));
        const bool exited = wait_for([&] { return second.state() == QProcess::NotRunning; }, 15000);
        check(exited);
        if (!exited) { second.kill(); second.waitForFinished(2000); }
        check(second.exitStatus() == QProcess::NormalExit && second.exitCode() == 0);
    };

    // Unsaved work in this window: the file opens beside it.
    s.renameTrack(0, "UNSAVED HERE");
    check(s.dirty());
    launch(project.fileName());
    ctx.reached("open running: a second launch hands the file over and exits");
    auto opened = await_window_report(child_png, 30000);
    check(opened.has_value() && opened->passed && opened->project == project.completeBaseName() &&
          opened->first_track == "OPENED FROM FILE");
    check(!asked() && s.dirty() && s.song().tracks[0].name == "UNSAVED HERE");
    ctx.reached("open running: unsaved work stays, and the file opens in a new window");
    // That window lets the song go before it is opened again below.
    check(stop_window(child_png, project.absoluteFilePath()));

    // A saved song is not replaced either; the file:// URL a file manager
    // hands over opens beside it too.
    s.renameTrack(0, "SAVED SOON");
    const QString other = QDir(project.absolutePath()).filePath("other.blok");
    check(c.saveProject(other));
    c.setWindowArgumentsForTesting(window_report_arguments(url_png));
    launch(QUrl::fromLocalFile(project.absoluteFilePath()).toString(QUrl::FullyEncoded));
    opened = await_window_report(url_png, 30000);
    check(opened.has_value() && opened->passed && opened->project == project.completeBaseName());
    check(!asked() && c.projectName() == "other" && s.song().tracks[0].name == "SAVED SOON");
    ctx.reached("open running: a saved song stays, and a file:// URL opens beside it");
    check(stop_window(url_png, project.absoluteFilePath()));

    // The file this window already holds only brings it forward.
    const QString before = c.status();
    launch(QFileInfo(other).fileName());
    VerifyContext::settle(100);
    check(c.status() == before && c.projectName() == "other" && !asked());
    ctx.reached("open running: the file this window holds just brings it forward");

    // A window holding nothing yet opens the file itself, without asking.
    c.newProject();
    check(wait_for([&] { return !s.dirty() && c.projectName() == "Untitled"; }, 5000));
    launch(project.fileName());
    check(wait_for([&] { return c.projectName() == project.completeBaseName(); }, 5000));
    check(!asked() && s.song().tracks[0].name == "OPENED FROM FILE" && !s.dirty());
    ctx.reached("open running: an untouched window opens the file itself");
    check(ctx.save_screenshot());
    ctx.finish("files opened while Blokkily runs never replace an open song");
}

[[maybe_unused]] const bool registered = register_scenario("open_running", run_open_running);

} // namespace
} // namespace blokkily::verify
