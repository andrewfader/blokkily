// features/notices.feature, run by bdd_notices: a failed save, a finished
// save and a plugin a song names that will not load are each said in a
// notice over the editors, readable in the rendered window; an error stays
// until its × is pressed and a "done" goes by itself.
#include "verify/harness.hpp"

#include "blokkily/project/project.hpp"

#include <QCoreApplication>
#include <QDir>

#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_notices(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto* const window = ctx.window;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "notices failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)window->grabWindow(); };
    const auto last = [&] {
        const auto notices = c.notices();
        return notices.isEmpty() ? QVariantMap{} : notices.back().toMap();
    };
    const QString work = QDir(ctx.parser.value("screenshot")).absolutePath() + QStringLiteral("-files");
    QDir(work).removeRecursively();
    QDir().mkpath(work);

    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    layout();
    auto* stack = ctx.named("noticeStack");
    check(stack != nullptr && c.notices().isEmpty());
    if (stack == nullptr) {
        ctx.finish("the notice stack was not found");
        return;
    }
    // Done notices go after a short wait here, so the gate need not sit
    // through the real one.
    stack->setProperty("doneFor", 300);

    // ------------------------------------------------- a save that fails
    const QString nowhere = QStringLiteral("/proc/blokkily-cannot/song.blok");
    check(!c.saveProjectFile(nowhere));
    layout();
    check(last().value("level") == "error" && last().value("text").toString().contains(nowhere));
    auto* text = ctx.named("noticeText0");
    auto* notice = ctx.named("notice0");
    std::cerr << "notice: " << (text ? text->property("text").toString().toStdString() : "none") << '\n';
    check(text && VerifyContext::usable(notice, 300, 30) && VerifyContext::reads_whole(text, text->property("text").toString()));
    // Over the editors, at the foot of the window.
    if (notice) {
        const auto bottom = notice->mapToScene({0, notice->height()}).y();
        check(bottom <= window->height() && bottom > window->height() - 120);
    }
    // An error is still there after a done notice would have gone.
    VerifyContext::settle(500);
    layout();
    check(c.notices().size() == 1);
    check(ctx.save_screenshot());
    ctx.reached("notices: a failed save is said, with its file and why, and stays");
    auto* dismiss = ctx.named("dismissNotice0");
    check(dismiss != nullptr);
    if (dismiss) ctx.click_at(dismiss, {dismiss->width() / 2, dismiss->height() / 2}, Qt::LeftButton);
    layout();
    check(c.notices().isEmpty());
    ctx.reached("notices: its × dismisses it");

    // ------------------------------------------------ a save that works
    check(c.saveProjectFile(work + QStringLiteral("/kept.blok")));
    layout();
    check(last().value("level") == "done" && last().value("text").toString().contains("kept.blok"));
    // A second save replaces the first's note rather than stacking on it.
    check(c.saveProjectFile(work + QStringLiteral("/kept-again.blok")));
    layout();
    check(c.notices().size() == 1 && last().value("text").toString().contains("kept-again.blok"));
    VerifyContext::settle(600);
    layout();
    check(c.notices().isEmpty());
    ctx.reached("notices: a finished save is said, and goes by itself");

    // ------------------------------------- a plugin that will not load
    {
        blokkily::Project project;
        project.song.tracks.front().name = "GHOST LEAD";
        project.song.tracks.front().instrument = {"CLAP", (work + QStringLiteral("/Ghost.clap")).toStdString(),
                                                  "dev.blokkily.ghost", {}};
        std::string error;
        check(blokkily::ProjectFile::save(project, (work + QStringLiteral("/ghost.blok")).toStdString(), &error));
    }
    check(c.loadProjectFile(work + QStringLiteral("/ghost.blok")));
    layout();
    const auto told = last().value("text").toString();
    std::cerr << "plugin notice: " << told.toStdString() << '\n';
    check(last().value("level") == "warning" && told.contains("GHOST LEAD") && told.contains("Ghost"));
    // Told once: the song rebuilt with the plugin still missing says nothing new.
    const auto count = c.notices().size();
    c.dismissNotice(last().value("id").toInt());
    c.reloadPlugins();
    layout();
    check(c.notices().size() == count - 1);
    ctx.reached("notices: a plugin the song names that will not load is named, once");

    QDir(work).removeRecursively();
    ctx.finish("failures and finished work said over the editors");
}

[[maybe_unused]] const bool registered = register_scenario("notices", run_notices);

} // namespace
} // namespace blokkily::verify
