// features/undo_history.feature, run by bdd_undo_history: history is a tree.
// An edit after an undo starts a branch and the steps undone stay; the
// rendered HISTORY panel lists every step by what it changed, and a click on
// one puts the song as it was there, whichever branch it is on.
#include "verify/harness.hpp"

#include "song_model.hpp"

#include <QCoreApplication>

#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_undo_history(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& song = ctx.song;
    auto* const window = ctx.window;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "undo history failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)window->grabWindow(); };
    const auto name = [&] { return QString::fromStdString(song.song().tracks.at(0).name); };
    const auto gain = [&] { return song.song().tracks.at(0).mix.gain_db; };
    const auto entries = [&] { return song.history(); };
    const auto find = [&](const QString& label) -> QVariantMap {
        for (const auto& entry : entries())
            if (entry.toMap().value("label").toString() == label) return entry.toMap();
        return {};
    };

    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    song.clearHistory();
    layout();
    check(!song.canUndo() && !song.canRedo() && entries().size() == 1);

    // Three steps on one line, then back one and a fourth: a branch.
    song.renameTrack(0, QStringLiteral("ONE"));
    song.renameTrack(0, QStringLiteral("TWO"));
    song.setTrackGain(0, -6.0);
    check(name() == "TWO" && gain() == -6.0);
    check(song.undo() && song.undo());
    check(name() == "ONE" && song.canRedo());
    song.setTrackGain(0, -12.0);
    check(name() == "ONE" && gain() == -12.0 && !song.canRedo());
    const auto listed = entries();
    for (const auto& entry : listed) {
        const auto map = entry.toMap();
        std::cerr << std::string(static_cast<std::size_t>(map.value("depth").toInt()) * 2, ' ')
                  << map.value("label").toString().toStdString()
                  << (map.value("current").toBool() ? "  <" : "") << '\n';
    }
    check(listed.size() == 5);
    const auto two = find(QStringLiteral("Track renamed TWO"));
    check(!two.isEmpty() && two.value("depth").toInt() == 0 && !two.value("onPath").toBool());
    // The branch made after the undo sits under the step it went on from.
    QVariantMap branch;
    for (const auto& entry : listed)
        if (entry.toMap().value("current").toBool()) branch = entry.toMap();
    check(branch.value("label").toString().endsWith("mix") && branch.value("depth").toInt() == 1);
    ctx.reached("undo history: an edit after an undo starts a branch, and the steps undone stay");

    // Back to the undone line: the song is as it was there.
    check(song.jumpToHistory(two.value("id").toLongLong()));
    layout();
    check(name() == "TWO" && gain() == 0.0);
    check(song.redo() && gain() == -6.0);
    check(song.undo() && song.undo() && name() == "ONE" && gain() == 0.0);
    // Redo from there follows the line last taken, not the branch.
    check(song.redo() && name() == "TWO");
    ctx.reached("undo history: any step can be gone back to, and redo follows the way it was reached");

    // Ctrl+H opens the panel; a click on the branch's step goes there.
    ctx.chord_key(Qt::Key_H, Qt::ControlModifier);
    layout();
    auto* panel = window->findChild<QObject*>("historyPanel");
    check(panel != nullptr && panel->property("opened").toBool());
    QQuickItem* target = nullptr;
    for (int index = 0; index < 8; ++index)
        if (auto* step = ctx.named(QStringLiteral("historyStep%1").arg(index)))
            if (step->property("modelData").toMap().value("id").toLongLong() == branch.value("id").toLongLong())
                target = step;
    check(target != nullptr && VerifyContext::usable(target, 200, 18));
    check(ctx.save_screenshot());
    if (target != nullptr) ctx.click_at(target, {target->width() / 2, target->height() / 2}, Qt::LeftButton);
    layout();
    check(name() == "ONE" && gain() == -12.0);
    ctx.chord_key(Qt::Key_H, Qt::ControlModifier);
    layout();
    check(panel != nullptr && !panel->property("opened").toBool());
    ctx.reached("undo history: the HISTORY panel lists every step, and a click goes there");

    ctx.finish("history is a tree that can be gone back along to any step");
}

[[maybe_unused]] const bool registered = register_scenario("undo_history", run_undo_history);

} // namespace
} // namespace blokkily::verify
