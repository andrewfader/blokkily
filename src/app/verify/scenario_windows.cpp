// features/multiple_windows.feature, run by bdd_multiple_windows: + WINDOW
// and OPEN NEW on the rendered project rail each start another real
// Blokkily process, which holds an empty song or the chosen project while
// this window keeps its own unsaved song.
//
// "opened_window" is the other end: the scenario a window this gate opened
// runs. It waits for the song it was started with, renders itself, writes
// what it holds beside its screenshot (verify/window_report.hpp) and exits.
#include "verify/harness.hpp"
#include "verify/window_report.hpp"
#include "song_model.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMetaObject>
#include <QSaveFile>
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

void run_opened_window(VerifyContext& ctx) {
    auto& c = ctx.controller;
    // The gate's opening check counts a default session's rows; a window
    // started with a song holds that song's instead, and judges it below.
    ctx.valid = true;
    ctx.failed.clear();
    const bool given_file = !ctx.parser.positionalArguments().isEmpty();
    // A window started with a file holds it once the launch's load is done.
    if (given_file) ctx.check(wait_for([&] { return c.projectName() != "Untitled"; }, 15000));
    VerifyContext::settle(200);
    ctx.check(ctx.save_screenshot());
    const auto& tracks = ctx.song.song().tracks;
    {
        // Written whole and renamed into place, so the gate never reads half.
        QSettings ini(window_report_path(ctx.parser.value("screenshot")) + ".part",
                      QSettings::IniFormat);
        ini.setValue("project", c.projectName());
        ini.setValue("first_track",
                     tracks.empty() ? QString() : QString::fromStdString(tracks[0].name));
        ini.setValue("title", ctx.window->title());
        ini.setValue("dirty", ctx.song.dirty());
        ini.setValue("passed", ctx.valid);
        ini.sync();
    }
    const auto report = window_report_path(ctx.parser.value("screenshot"));
    QFile::remove(report);
    ctx.check(QFile::rename(report + ".part", report));
    ctx.finish("a window a gate opened reported what it holds");
}

void run_windows(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "multiple windows failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const QFileInfo project(ctx.parser.value("project"));
    check(project.exists());
    const QString child_png =
        QFileInfo(ctx.parser.value("screenshot")).path() + "/multiple-windows-opened.png";
    c.setWindowArgumentsForTesting(window_report_arguments(child_png));

    // Unsaved work in this window, which neither new window may touch.
    s.renameTrack(0, "STAYS HERE");
    check(s.dirty());
    auto* dialog = ctx.window->findChild<QObject*>("discardDialog");
    check(dialog != nullptr);

    // + WINDOW: another, empty song beside this one.
    forget_window_report(child_png);
    auto* new_window = ctx.named("newWindowButton");
    check(VerifyContext::usable(new_window, 40, 16));
    if (new_window)
        ctx.click_at(new_window, {new_window->width() / 2, new_window->height() / 2},
                     Qt::LeftButton);
    const auto empty = await_window_report(child_png, 30000);
    check(empty.has_value());
    if (empty) {
        check(empty->passed && empty->project == "Untitled" && !empty->dirty);
        check(empty->first_track != "STAYS HERE");
        check(empty->title.contains("Untitled"));
    }
    check(s.dirty() && s.song().tracks[0].name == "STAYS HERE" && c.projectName() == "Untitled");
    check(!(dialog && dialog->property("opened").toBool()));
    ctx.reached("multiple windows: + WINDOW opens an empty song beside this one");

    // OPEN NEW: the chosen project in a window of its own, without
    // asking about this window's work.
    forget_window_report(child_png);
    auto* open_window = ctx.named("openWindowButton");
    check(VerifyContext::usable(open_window, 40, 16));
    if (open_window)
        ctx.click_at(open_window, {open_window->width() / 2, open_window->height() / 2},
                     Qt::LeftButton);
    auto* chooser = ctx.window->findChild<QObject*>("openProjectDialog");
    check(chooser != nullptr &&
          wait_for([&] { return chooser->property("visible").toBool(); }, 3000));
    check(!(dialog && dialog->property("opened").toBool()));
    check(chooser && chooser->property("newWindow").toBool());
    // A frame with the chooser up, for the record.
    VerifyContext::settle(200);
    check(ctx.save_screenshot_to(QFileInfo(ctx.parser.value("screenshot")).path() +
                                 "/multiple-windows-choose.png"));
    if (chooser) {
        // The producer's choice, as the dialog reports it: accept() would take
        // the offscreen dialog's own (empty) selection instead.
        const auto chosen = QUrl::fromLocalFile(project.absoluteFilePath());
        chooser->setProperty("selectedFile", chosen);
        check(chooser->property("selectedFile").toUrl() == chosen);
        QMetaObject::invokeMethod(chooser, "accepted");
        QMetaObject::invokeMethod(chooser, "close");
    }
    const auto opened = await_window_report(child_png, 30000);
    check(opened.has_value());
    if (opened) {
        check(opened->passed && opened->project == project.completeBaseName());
        check(opened->first_track == "OPENED FROM FILE" && !opened->dirty);
        check(opened->title.contains(project.completeBaseName()));
    }
    check(s.dirty() && s.song().tracks[0].name == "STAYS HERE" && c.projectName() == "Untitled");
    ctx.reached("multiple windows: OPEN NEW opens the project beside this song");

    // Ctrl+O still opens in this window, asking first. A shortcut answers
    // only in the focused window, and a chooser drawn as a window of its own
    // leaves none focused when it closes, as a producer's click would not.
    check(!chooser || wait_for([&] { return !chooser->property("visible").toBool(); }, 3000));
    ctx.window->requestActivate();
    check(wait_for([&] { return QGuiApplication::focusWindow() == ctx.window; }, 3000));
    ctx.chord_key(Qt::Key_O, Qt::ControlModifier);
    const bool asked = dialog && wait_for([&] { return dialog->property("opened").toBool(); }, 3000);
    if (!asked)
        std::cerr << "Ctrl+O asked nothing; chooser visible: "
                  << (chooser && chooser->property("visible").toBool()) << ", focus window: "
                  << (QGuiApplication::focusWindow() ? qPrintable(QGuiApplication::focusWindow()->objectName())
                                                     : "none")
                  << '\n';
    check(asked);
    if (dialog) QMetaObject::invokeMethod(dialog, "reject");
    check(wait_for([&] { return !(dialog && dialog->property("opened").toBool()); }, 3000));
    check(s.song().tracks[0].name == "STAYS HERE");
    ctx.reached("multiple windows: Ctrl+O still replaces this window's song, asking first");

    check(ctx.save_screenshot());
    ctx.finish("several songs open at once, each in a window of its own");
}

[[maybe_unused]] const bool registered_windows = register_scenario("windows", run_windows);
[[maybe_unused]] const bool registered_opened =
    register_scenario("opened_window", run_opened_window);

} // namespace
} // namespace blokkily::verify
