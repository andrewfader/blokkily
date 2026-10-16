// features/open_in_running_window.feature, run by bdd_open_running: a second
// `blokkily song.blok`, started as the desktop starts it, hands the file to
// this window and exits; this window asks about its unsaved work and opens
// the song.
#include "verify/harness.hpp"
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

QQuickItem* find_text(QQuickItem* root, const QString& text) {
    if (root == nullptr) return nullptr;
    if (root->property("text").toString() == text) return root;
    for (auto* child : root->childItems())
        if (auto* found = find_text(child, text)) return found;
    return nullptr;
}

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

    // Unsaved work in this window.
    s.renameTrack(0, "UNSAVED HERE");
    check(s.dirty());

    // The desktop's launch: the real executable, with the file named
    // relative to the folder it runs in, and nothing to tell it a window is
    // open but the channel.
    QProcess second;
    second.setProgram(QCoreApplication::applicationFilePath());
    second.setArguments({project.fileName()});
    second.setWorkingDirectory(project.absolutePath());
    auto environment = QProcessEnvironment::systemEnvironment();
    // Were the hand-over to fail it would open a window of its own; keep that
    // window's autosave out of the user's folder.
    environment.insert("BLOKKILY_RECOVERY_DIR", QDir(project.absolutePath()).filePath("second-recovery"));
    environment.insert("BLOKKILY_MCP", "0");
    second.setProcessEnvironment(environment);
    second.start();
    check(second.waitForStarted(5000));
    const bool exited = wait_for([&] { return second.state() == QProcess::NotRunning; }, 15000);
    check(exited);
    if (!exited) { second.kill(); second.waitForFinished(2000); }
    check(second.exitStatus() == QProcess::NormalExit && second.exitCode() == 0);
    ctx.reached("open running: a second launch hands the file over and exits");

    // This window asks before the open song is replaced.
    auto* dialog = ctx.window->findChild<QObject*>("discardDialog");
    check(dialog != nullptr && wait_for([&] { return dialog->property("opened").toBool(); }, 3000));
    check(s.song().tracks[0].name == "UNSAVED HERE");
    QQuickItem* discard = dialog ? find_text(qvariant_cast<QQuickItem*>(dialog->property("footer")), "Discard")
                                 : nullptr;
    check(VerifyContext::usable(discard, 40, 18));
    // A frame for the dialog to be drawn in before it is captured.
    VerifyContext::settle(300);
    const QString asked_png = QFileInfo(ctx.parser.value("screenshot")).path() + "/open-running-asks.png";
    check(ctx.save_screenshot_to(asked_png));
    if (discard) ctx.click_at(discard, {discard->width() / 2, discard->height() / 2}, Qt::LeftButton);
    check(wait_for([&] { return c.projectName() == project.completeBaseName(); }, 5000));
    check(!s.song().tracks.empty() && s.song().tracks[0].name == "OPENED FROM FILE");
    check(!s.dirty());
    ctx.reached("open running: this window opens the song once its work is let go");

    // A clean window opens the next one without asking.
    s.renameTrack(0, "SAVED SOON");
    check(c.saveProject(QDir(project.absolutePath()).filePath("other.blok")));
    second.setArguments({QUrl::fromLocalFile(project.absoluteFilePath()).toString(QUrl::FullyEncoded)});
    second.start();
    check(second.waitForStarted(5000));
    check(wait_for([&] { return second.state() == QProcess::NotRunning; }, 15000));
    check(second.exitCode() == 0);
    check(wait_for([&] { return c.projectName() == project.completeBaseName(); }, 5000));
    check(!(dialog && dialog->property("opened").toBool()));
    check(s.song().tracks[0].name == "OPENED FROM FILE");
    ctx.reached("open running: a clean window opens a file:// URL at once");
    check(ctx.save_screenshot());
    ctx.finish("files opened while Blokkily runs go to the running window");
}

[[maybe_unused]] const bool registered = register_scenario("open_running", run_open_running);

} // namespace
} // namespace blokkily::verify
