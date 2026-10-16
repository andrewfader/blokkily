// features/insert_locks.feature, run on its own by the bdd_insert_locks gate
// (`--scenario insert_locks`). The CLAP synth plays track 0 through the CLAP
// effect fixture, whose output is its input times its Gain (0.25 until told
// otherwise). A step's lock is pointed at the effect from the rendered
// inspector's ON and PARAM pickers. Played from that step, through the
// production render callback, the track is heard at twice the level, the
// tracker marks the lock as the first effect's, and undo takes it back and
// the effect plays its own Gain again. Then the ON picker points the lock at
// the master's CLAP effect, heard on the master.

#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QMetaObject>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

namespace blokkily::verify {
namespace {

void run_insert_locks(VerifyContext& ctx) {
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
    std::ostringstream details;

    // The CLAP synth on track 0, soloed, and the CLAP effect after it.
    check(controller.verifyClap(parser.value("clap-fixture")));
    controller.setTempo(120.0);
    song.selectTrack(0);
    song.selectPattern(0);
    song.toggleSolo(0);
    check(song.addInsert(BusKind::track, 0,
                         {"CLAP", parser.value("clap-effect-fixture").toStdString(),
                          "dev.blokkily.test.effect", {}}));
    controller.flushRecompile();
    lay_out();
    check(controller.engine() != nullptr &&
          controller.engine()->processor({BusKind::track, 0, 0}) != nullptr);
    check(window->setProperty("view", "ALL"));

    // One note held across step 0 and the three after it, nothing else.
    for (int step = 0; step < 4; ++step) pattern.clearStep(step);
    pattern.setStepKey(0, 60);
    pattern.selectStep(0);
    pattern.setSelectedDuration(480);
    lay_out();
    reached("insert locks: the synth plays through the CLAP effect");

    // How loud track 0 is over its first step, played from its start.
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
    const float unlocked = step_level();
    check(unlocked > 1e-3F);
    details << " | unlocked " << unlocked;

    // The inspector, scrolled into the window the way a producer scrolls
    // to it.
    const auto show_inspector = [&] {
        lay_out();
        if (auto* scroll = ctx.named("editorScroll")) {
            scroll->setProperty("contentY", std::max(0.0,
                scroll->property("contentHeight").toDouble() - scroll->height()));
            QCoreApplication::processEvents();
        }
        lay_out();
    };
    const auto on_screen = [window](const QQuickItem* item) {
        if (item == nullptr) return false;
        const auto top_left = item->mapToScene({0, 0});
        return top_left.y() >= 0 && top_left.x() >= 0 &&
               top_left.y() + item->height() <= window->height() &&
               top_left.x() + item->width() <= window->width();
    };
    // Row `row` of a picker's menu, clicked in the rendered window.
    const auto pick = [&](const char* picker, int row) {
        auto* owner = ctx.named(QString::fromLatin1(picker));
        check(owner != nullptr && row >= 0);
        if (owner == nullptr || row < 0) return;
        ctx.click_at(owner, {owner->width() / 2, owner->height() / 2}, Qt::LeftButton);
        lay_out();
        auto* menu = owner->findChild<QObject*>(QString::fromLatin1(picker) + "Menu");
        check(menu != nullptr && menu->property("opened").toBool());
        QQuickItem* entry = nullptr;
        if (menu != nullptr)
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, entry),
                                      Q_ARG(int, row));
        check(entry != nullptr);
        if (entry != nullptr)
            ctx.click_at(entry, {entry->width() / 2, entry->height() / 2}, Qt::LeftButton);
        lay_out();
    };

    show_inspector();
    auto* target = ctx.named("lockTarget");
    auto* parameter = ctx.named("lockParameter");
    check(VerifyContext::usable(target, 60, 18) && VerifyContext::usable(parameter, 40, 18));
    check(on_screen(target) && on_screen(parameter));
    const auto targets = song.lockTargets(0);
    check(targets.size() == 2 && targets.at(0) == "INSTRUMENT" &&
          targets.at(1).startsWith("FX 1 · "));
    check(target != nullptr && target->property("value").toString() == "INSTRUMENT");
    reached("insert locks: the inspector offers the track's instrument and its effect");

    // ON: the effect. PARAM: its Gain. A new lock starts at 0.5.
    pick("lockTarget", static_cast<int>(targets.indexOf(targets.value(1))));
    pick("lockParameter", 0);
    const auto selected = pattern.selected();
    check(selected.value("hasLock").toBool() && selected.value("lockSlot").toInt() == 0 &&
          selected.value("lockName").toString() == "Gain" &&
          std::abs(selected.value("lockValue").toDouble() - 0.5) < 1e-9);
    check(target != nullptr && target->property("value").toString() == targets.value(1));
    check(parameter != nullptr && parameter->property("value").toString() == "Gain");
    const auto lock_text = pattern.steps().at(0).toMap().value("lockText").toString();
    check(lock_text.startsWith("F1A"));
    details << " | tracker " << lock_text.toStdString();
    reached("insert locks: ON and PARAM point the step's lock at the effect's Gain");

    // Heard: the effect's Gain is 0.5 from the step on, twice its own 0.25;
    // the instrument is untouched.
    const float locked = step_level();
    details << " | locked " << locked;
    check(unlocked > 0.0F && std::abs(locked / unlocked - 2.0F) < 0.02F);
    if (!(std::abs(locked / unlocked - 2.0F) < 0.02F))
        std::cerr << "insert locks: the locked step is heard at " << locked << ", "
                  << (unlocked > 0.0F ? locked / unlocked : 0.0F) << " times the unlocked "
                  << unlocked << ", not twice\n";
    reached("insert locks: the step is heard through the effect at its locked Gain");

    // Each pick is one step of history: undone, the step has no lock and the
    // effect is back at its own Gain, and redone it is heard at the locked
    // Gain again.
    const bool undone_twice = song.undo() && song.undo();
    lay_out();
    check(undone_twice && !pattern.selected().value("hasLock").toBool() &&
          pattern.steps().at(0).toMap().value("lockText").toString() == "---");
    controller.flushRecompile();
    const float restored = step_level();
    details << " | restored " << restored;
    check(std::abs(restored / unlocked - 1.0F) < 0.02F);
    if (!(std::abs(restored / unlocked - 1.0F) < 0.02F))
        std::cerr << "insert locks: with the lock removed the step is heard at " << restored
                  << ", not its unlocked " << unlocked << '\n';
    reached("insert locks: removing the lock puts the effect's Gain back");
    check(song.redo() && song.redo());
    lay_out();
    check(pattern.selected().value("lockSlot").toInt() == 0 &&
          pattern.selected().value("lockName").toString() == "Gain");
    const float redone = step_level();
    check(std::abs(redone / unlocked - 2.0F) < 0.02F);
    details << " | redone " << redone;
    reached("insert locks: undo and redo");

    // The master's CLAP effect, offered to every track's steps. The step's
    // lock is cleared, then pointed at the master's Gain from ON.
    pattern.clearSelectedLock();
    check(song.addInsert(BusKind::master, 0,
                         {"CLAP", parser.value("clap-effect-fixture").toStdString(),
                          "dev.blokkily.test.effect", {}}));
    controller.flushRecompile();
    lay_out();
    const auto master_level = [&] {
        controller.seekToStep(0);
        controller.togglePlayback();
        float loudest = 0.0F;
        for (int block = 0; block < 6; ++block) {
            (void)ctx.pump();
            if (controller.engine() != nullptr)
                loudest = std::max(loudest, controller.engine()->master_peak());
        }
        controller.togglePlayback();
        (void)ctx.pump();
        return loudest;
    };
    const float master_own = master_level();
    details << " | master " << master_own;
    check(master_own > 1e-4F);
    show_inspector();
    QStringList names;
    for (const auto& choice : song.lockTargetChoices(0)) names << choice.toMap().value("name").toString();
    qsizetype master_row = -1;
    for (qsizetype row = 0; row < names.size() && master_row < 0; ++row)
        if (names[row].startsWith(QStringLiteral("MASTER · FX 1 · "))) master_row = row;
    check(master_row == 2);
    pick("lockTarget", static_cast<int>(master_row));

    pick("lockParameter", 0);
    const auto on_master = pattern.selected();
    check(on_master.value("lockKind").toString() == "master" &&
          on_master.value("lockSlot").toInt() == 0 &&
          on_master.value("lockName").toString() == "Gain");
    check(target != nullptr && target->property("value").toString() == names.value(master_row));
    const auto master_text = pattern.steps().at(0).toMap().value("lockText").toString();
    check(master_text.startsWith("MF1A"));
    const float master_locked = master_level();
    details << " | master locked " << master_locked << " (" << master_text.toStdString() << ")";
    check(std::abs(master_locked / master_own - 2.0F) < 0.02F);
    if (!(std::abs(master_locked / master_own - 2.0F) < 0.02F))
        std::cerr << "insert locks: the master locked is heard at " << master_locked << ", not twice "
                  << master_own << '\n';
    reached("insert locks: ON points the lock at the master's effect, heard on the master");

    controller.seekToStep(0);
    pattern.selectStep(0);
    VerifyContext::settle(40);
    show_inspector();
    check(on_screen(ctx.named("lockTarget")));
    reached("insert locks: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("insert_locks", run_insert_locks);

}  // namespace
}  // namespace blokkily::verify
