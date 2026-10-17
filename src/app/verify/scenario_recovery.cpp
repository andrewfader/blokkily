// features/autosave.feature, run by the bdd_recovery_* gates. recovery_crash
// works on a song, lets autosave copy it aside, and then dies without
// cleaning up, as a crash does. recovery_restore is the next launch: it is
// offered the song back, recovers it, and lets an older one go.
#include "verify/harness.hpp"
#include "song_model.hpp"
#include "blokkily/project/project.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSettings>
#include <QSysInfo>
#include <QTimeZone>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

QString store() { return qEnvironmentVariable("BLOKKILY_RECOVERY_DIR"); }

int entries() { return static_cast<int>(QDir(store()).entryList({"*.blok"}, QDir::Files).size()); }

// Runs the event loop until `ready` holds, for at most `ms`.
template <typename Ready> bool wait_for(Ready ready, int ms) {
    QElapsedTimer clock;
    clock.start();
    while (!ready() && clock.elapsed() < ms) VerifyContext::settle(20);
    return ready();
}

void run_recovery_crash(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "recovery crash failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    check(!store().isEmpty());
    // A clean store, so what the next launch finds is this run's alone.
    for (const auto& entry : c.recoveries()) (void)c.discardRecovery(entry.toMap().value("id").toString());
    for (const auto& file : QDir(store()).entryList({"*.blok", "*.info"}, QDir::Files))
        QFile::remove(QDir(store()).filePath(file));
    // A lock a dead window left with no song beside it is pruned by the next
    // window to start; this one already has, so only its own lock is here.
    check(QDir(store()).entryList({"*.lock"}, QDir::Files).size() == 1);

    // Nothing unsaved, nothing copied.
    check(!s.dirty());
    check(c.autosaveNow() && entries() == 0);
    // An edit is copied aside by the timer, not only when asked.
    s.renameTrack(0, "UNSAVED IDEA");
    check(s.dirty());
    check(wait_for([] { return entries() == 1; }, 3000));
    // This window's own copy is never offered to it: it is still running.
    check(c.recoveries().isEmpty());
    ctx.reached("recovery: an unsaved edit is autosaved");

    // Saving puts the song on disk and takes the copy away.
    check(c.saveProject(ctx.parser.value("project")));
    check(!s.dirty() && entries() == 0);
    s.renameTrack(0, "AFTER SAVE");
    check(c.autosaveNow() && entries() == 1);
    ctx.reached("recovery: a save removes the copy, the next edit makes one");

    // An older song a window left untitled, for the next launch to let go.
    blokkily::Project older;
    older.song = s.song();
    older.song.tracks[0].name = "DISCARD ME";
    QFile old_song(QDir(store()).filePath("older-window.blok"));
    check(old_song.open(QIODevice::WriteOnly) &&
          old_song.write(QByteArray::fromStdString(blokkily::ProjectFile::serialize(older))) > 0);
    old_song.close();
    {
        QSettings info(QDir(store()).filePath("older-window.info"), QSettings::IniFormat);
        info.setValue("project", QString());
        info.setValue("title", "Untitled");
        info.setValue("saved", QDateTime(QDate(2026, 1, 2), QTime(3, 4), QTimeZone::UTC));
    }
    // And a lock a window that died with nothing unsaved left: no song beside
    // it, its process long gone.
    QFile dead(QDir(store()).filePath("dead-window.lock"));
    check(dead.open(QIODevice::WriteOnly) &&
          dead.write(QStringLiteral("999999\nblokkily\n%1\n").arg(QSysInfo::machineHostName()).toUtf8()) > 0);
    dead.close();
    ctx.reached("recovery: the window dies without closing");
    // A crash: no destructor runs, the lock file stays, the copy stays.
    std::cout << (ctx.valid ? "BDD PASS" : "BDD FAIL")
              << (ctx.failed.isEmpty() ? "" : " at: " + ctx.failed.toStdString())
              << " recovery crash left its copy behind" << std::endl;
    std::fflush(nullptr);
    std::_Exit(ctx.valid ? 0 : 3);
}

void run_recovery_restore(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "recovery restore failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    layout();
    // The next window to start prunes a dead window's lock that has no song.
    check(!QFile::exists(QDir(store()).filePath("dead-window.lock")));
    const auto offered = c.recoveries();
    check(offered.size() == 2);
    if (offered.size() != 2) { ctx.finish("recovery offered nothing"); return; }
    // Newest first: the song the crashed window was working on.
    const auto newest = offered[0].toMap();
    const QString project = ctx.parser.value("project");
    check(QFileInfo(newest.value("project").toString()) == QFileInfo(project));
    check(newest.value("title").toString() == QFileInfo(project).completeBaseName());
    check(offered[1].toMap().value("title").toString() == "Untitled");
    // The window asks, with buttons a producer can press.
    auto* popup = ctx.named("recoveryMessage");
    check(VerifyContext::usable(popup, 200, 30));
    check(popup && popup->property("text").toString().contains("was not saved"));
    auto* recover = ctx.named("recoverButton");
    check(VerifyContext::usable(recover, 50, 18));
    check(VerifyContext::usable(ctx.named("discardRecoveryButton"), 50, 18));
    const QString offer_png = QFileInfo(ctx.parser.value("screenshot")).path() + "/recovery-offer.png";
    check(ctx.save_screenshot_to(offer_png));
    ctx.reached("recovery: the next launch offers the song back");

    if (recover) ctx.click_at(recover, {recover->width() / 2, recover->height() / 2}, Qt::LeftButton);
    layout();
    // The song as it was when the window died, unsaved, as that project.
    check(!s.song().tracks.empty() && s.song().tracks[0].name == "AFTER SAVE");
    check(s.dirty());
    check(c.projectName() == QFileInfo(project).completeBaseName());
    check(c.projectStatus() == "Recovered unsaved work");
    // Its old copy is gone; this window keeps its own from now on.
    check(c.recoveries().size() == 1);
    check(entries() == 2);
    ctx.reached("recovery: RECOVER opens it unsaved, as its project");

    // The older one is offered next and let go.
    auto* discard = ctx.named("discardRecoveryButton");
    check(VerifyContext::usable(discard, 50, 18));
    if (discard) ctx.click_at(discard, {discard->width() / 2, discard->height() / 2}, Qt::LeftButton);
    layout();
    check(c.recoveries().isEmpty());
    check(!QFile::exists(QDir(store()).filePath("older-window.blok")));
    check(s.song().tracks[0].name == "AFTER SAVE");
    ctx.reached("recovery: DISCARD lets an old copy go");

    // Saving writes it back where it came from and the copy goes.
    check(c.saveProjectInPlace());
    check(!s.dirty() && entries() == 0);
    const auto saved = blokkily::ProjectFile::load(project.toStdString());
    check(saved && saved->song.tracks[0].name == "AFTER SAVE");
    layout();
    check(ctx.save_screenshot());
    ctx.finish("an autosaved song survives a crash and is recovered as its project");
}

[[maybe_unused]] const bool crash = register_scenario("recovery_crash", run_recovery_crash);
[[maybe_unused]] const bool restore = register_scenario("recovery_restore", run_recovery_restore);

} // namespace
} // namespace blokkily::verify
