// features/versions.feature, run on its own by the bdd_versions gate
// (`--scenario versions`). In the rendered window: NOTES takes typed text into
// the song as one step of history; VERSIONS saves the song as named versions,
// and RESTORE brings one back, heard through the production render callback
// from the CLAP fixture, as one step of history; both survive a save and a
// load; DELETE asks twice.

#include "verify/harness.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

namespace blokkily::verify {
namespace {

void run_versions(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    auto& controller = ctx.controller;
    const auto& parser = ctx.parser;
    const auto check = [&ctx](bool ok) { ctx.check(ok); };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    const auto click = [&](const char* name) {
        auto* item = ctx.named(QString::fromLatin1(name));
        if (item == nullptr || !item->isVisible()) return false;
        ctx.click_at(item, {item->width() / 2, item->height() / 2}, Qt::LeftButton);
        lay_out();
        return true;
    };
    const auto type = [&](const QString& text) {
        for (const QChar c : text)
            ctx.type_key(static_cast<Qt::Key>(c.toUpper().unicode()), QString(c));
        lay_out();
    };
    std::ostringstream details;

    // The CLAP synth on track 0, a note on step 0 and nothing else there.
    check(controller.verifyClap(parser.value("clap-fixture")));
    controller.setTempo(120.0);
    song.selectTrack(0);
    song.selectPattern(0);
    for (int step = 0; step < 4; ++step) pattern.clearStep(step);
    pattern.setStepKey(0, 60);
    lay_out();
    // How loud track 0 is over step 0, played from its start.
    const auto step_level = [&] {
        controller.seekToStep(0);
        controller.togglePlayback();
        float loudest = 0.0F;
        for (int block = 0; block < 6; ++block) {
            (void)ctx.pump();
            if (controller.engine() != nullptr)
                loudest = std::max(loudest, controller.engine()->track_peak(0));
        }
        controller.togglePlayback();
        (void)ctx.pump();
        return loudest;
    };
    const float with_note = step_level();
    check(with_note > 1e-3F);

    // The tabs: both at a usable size beside the others.
    check(VerifyContext::usable(ctx.named("viewVERSIONS"), 24, 18) &&
          VerifyContext::usable(ctx.named("viewNOTES"), 24, 18));
    const auto fits = [window](const char* name, VerifyContext& context) {
        auto* item = context.named(QString::fromLatin1(name));
        if (item == nullptr) return false;
        const auto right = item->mapToScene({item->width(), 0}).x();
        return right <= window->width();
    };
    check(fits("viewNOTES", ctx) && fits("rescanButton", ctx));
    reached("versions: VERSIONS and NOTES sit with the other views");

    // NOTES: typed text is the song's, one step of history.
    check(click("viewNOTES"));
    check(window->property("view").toString() == "NOTES");
    check(VerifyContext::usable(ctx.named("notesPanel"), 300, 300));
    // The editors give it their place.
    auto* editors = ctx.named("stepEditors");
    check(editors != nullptr && !editors->isVisible());
    check(click("notesText"));
    type(QStringLiteral("hook in bar nine"));
    check(song.notes() == "hook in bar nine");
    check(song.undo());
    lay_out();
    // The box shows what the song holds, after an undo as after typing.
    check(song.notes().isEmpty() && ctx.named("notesText") != nullptr &&
          ctx.named("notesText")->property("text").toString().isEmpty());
    check(song.redo());
    lay_out();
    check(song.notes() == "hook in bar nine");
    check(ctx.named("notesText") != nullptr &&
          ctx.named("notesText")->property("text").toString() == "hook in bar nine");
    // Undo and redo leave the caret where it was, not at the start.
    check(ctx.named("notesText") != nullptr &&
          ctx.named("notesText")->property("cursorPosition").toInt() > 0);
    // The NOTES view, as evidence to look at, beside the gate's screenshot.
    if (parser.isSet("screenshot")) {
        QString path = parser.value("screenshot");
        path = path.left(path.size() - 4) + QStringLiteral("-notes.png");
        check(window->grabWindow().save(path));
    }
        reached("versions: NOTES keeps the song's notes, one step of history");

    // VERSIONS: FIRST saved as the song is, then the note taken away and the
    // tempo changed, and SECOND saved.
    check(click("viewVERSIONS"));
    check(VerifyContext::usable(ctx.named("versionsPanel"), 300, 300));
    check(click("versionName"));
    type(QStringLiteral("FIRST"));
    check(click("saveVersion"));
    check(song.versions().size() == 1 && song.versions().front().name == "FIRST");
    pattern.clearStep(0);
    controller.setTempo(90.0);
    controller.flushRecompile();
    check(step_level() < 1e-6F);
    check(click("saveVersion"));
    check(song.versions().size() == 2 && song.versions().back().name == "VERSION 2");
    lay_out();
    // Newest first: row 0 is VERSION 2, row 1 FIRST.
    auto* newest = ctx.named("versionTitle0");
    auto* oldest = ctx.named("versionTitle1");
    check(newest != nullptr && newest->property("text").toString() == "VERSION 2" &&
          oldest != nullptr && oldest->property("text").toString() == "FIRST");
    reached("versions: SAVE VERSION keeps the song as it is, newest first");

    // Each row says how its version differs from the song now: FIRST had the
    // note and 120 BPM.
    auto* first_difference = ctx.named("versionDifference1");
    const auto first_says = first_difference != nullptr
                                ? first_difference->property("text").toString() : QString();
    check(first_says.startsWith("differs: ") && first_says.contains("120 BPM, now 90") &&
          first_says.contains("note"));
    auto* second_difference = ctx.named("versionDifference0");
    check(second_difference != nullptr &&
          second_difference->property("text").toString().startsWith("same tempo"));
    details << " | FIRST " << first_says.toStdString();

    // RESTORE FIRST: the note and the tempo are back, and heard. The song it
    // replaced is VERSION 2 exactly, so it is not saved again.
    check(click("restoreVersion1"));
    controller.flushRecompile();
    lay_out();
    check(pattern.hasStep(0) && std::abs(song.song().tempo.bpm_at(0) - 120.0) < 1e-9);
    const float restored = step_level();
    check(std::abs(restored - with_note) < 1e-4F);
    check(song.versions().size() == 2);
    details << " | heard " << with_note << " → " << restored;
    // FIRST now matches the song; VERSION 2 differs.
    lay_out();
    check(ctx.named("versionDifference1") != nullptr &&
          ctx.named("versionDifference1")->property("text").toString().startsWith("same tempo"));
    reached("versions: RESTORE brings a version back, heard as it was");

    // One step of history: undo brings back the song RESTORE replaced, and the
    // versions stay as they are.
    check(song.undo());
    controller.flushRecompile();
    lay_out();
    check(!pattern.hasStep(0) && std::abs(song.song().tempo.bpm_at(0) - 90.0) < 1e-9);
    check(song.versions().size() == 2);
    check(song.redo());
    controller.flushRecompile();
    lay_out();
    check(pattern.hasStep(0));
    reached("versions: undo takes a restore back, never a version");

    // A song that is no version yet is kept as one before a restore replaces
    // it: BEFORE FIRST, and restoring that brings the change back.
    controller.setTempo(100.0);
    controller.flushRecompile();
    lay_out();
    check(click("restoreVersion1"));
    controller.flushRecompile();
    lay_out();
    check(song.versions().size() == 3 && song.versions().back().name == "BEFORE FIRST");
    check(std::abs(song.song().tempo.bpm_at(0) - 120.0) < 1e-9);
    // Newest first: BEFORE FIRST is row 0.
    check(ctx.named("versionTitle0") != nullptr &&
          ctx.named("versionTitle0")->property("text").toString() == "BEFORE FIRST");
    check(click("restoreVersion0"));
    controller.flushRecompile();
    lay_out();
    check(std::abs(song.song().tempo.bpm_at(0) - 100.0) < 1e-9);
    check(song.versions().size() == 3);
    reached("versions: a restore keeps the song it replaces as a version");

    // RENAME edits the name in place.
    check(click("versionRename0"));
    auto* field = ctx.named("versionRenameField0");
    check(field != nullptr && field->isVisible() && field->hasActiveFocus());
    if (!(field != nullptr && field->isVisible() && field->hasActiveFocus()))
        std::cerr << "versions: rename field " << (field != nullptr) << " visible "
                  << (field != nullptr && field->isVisible()) << " focused "
                  << (field != nullptr && field->hasActiveFocus()) << '\n';
    type(QStringLiteral("SPARE"));
    ctx.type_key(Qt::Key_Return, QStringLiteral("\r"));
    lay_out();
    check(song.versions().back().name == "SPARE" && ctx.named("versionTitle0") != nullptr &&
          ctx.named("versionTitle0")->property("text").toString() == "SPARE");
    reached("versions: RENAME edits a version's name in place");

    // Saved and loaded, the versions and the notes come back.
    const QString project = parser.value("project");
    check(!project.isEmpty());
    if (!project.isEmpty()) {
        check(controller.saveProjectFile(project));
        check(!song.dirty());
        check(controller.loadProjectFile(project));
        VerifyContext::settle(40);
        lay_out();
        check(song.versions().size() == 3 && song.versions().front().name == "FIRST" &&
              song.versions().back().name == "SPARE");
        check(song.notes() == "hook in bar nine");
        check(ctx.named("notesText") != nullptr &&
              ctx.named("notesText")->property("text").toString() == "hook in bar nine");
    }
    reached("versions: versions and notes survive a save and a load");

    // DELETE asks twice.
    window->setProperty("view", "VERSIONS");
    lay_out();
    check(click("deleteVersion0"));
    check(song.versions().size() == 3 && ctx.named("deleteVersion0") != nullptr &&
          ctx.named("deleteVersion0")->property("text").toString() == "SURE?");
    check(click("deleteVersion0"));
    check(song.versions().size() == 2 && song.versions().front().name == "FIRST" &&
          song.versions().back().name == "VERSION 2");
    check(song.dirty());
    reached("versions: DELETE asks twice");

    check(click("versionName"));
    type(QStringLiteral("MIX TWO"));
    check(click("saveVersion"));
    lay_out();
    // A version saved after a rename shows its name, not a rename box (a
    // regression: the rename left the panel renaming whichever row came next).
    auto* newest_title = ctx.named("versionTitle0");
    auto* newest_field = ctx.named("versionRenameField0");
    const bool named_row = newest_title != nullptr && newest_title->isVisible() &&
                           newest_title->property("text").toString() == "MIX TWO" &&
                           (newest_field == nullptr || !newest_field->isVisible());
    check(named_row);
    if (!named_row) std::cerr << "REGRESSION: the newest version opened in rename mode\n";
    check(ctx.save_screenshot());
    reached("screenshot");
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("versions", run_versions);

}  // namespace
}  // namespace blokkily::verify
