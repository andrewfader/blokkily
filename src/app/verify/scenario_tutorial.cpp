// features/tutorial.feature, run on its own by the bdd_tutorial gate
// (`--scenario tutorial`). Everything is driven through the rendered window:
// HELP is clicked, the tour's own buttons are clicked and its keys typed, and
// what is checked is where the tour drew its ring, bubble and mascot against
// where the controls it names were laid out, and that it is gone afterwards.

#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QRectF>

#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

// The stops the feature promises, in order. Each is the objectName of the
// control the ring is drawn round; the first has none.
constexpr const char* stops[] = {
    "",           "playButton",      "tempoBox",   "viewSwitcher", "arrangementView",
    "loopButton", "stepGrid",        "instrumentPanel", "mixerPanel", "promptBar",
    "helpButton",
};
constexpr int stop_count = static_cast<int>(sizeof(stops) / sizeof(stops[0]));

QRectF scene_rect(const QQuickItem* item) {
    if (item == nullptr) return {};
    return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
}

void run_tutorial(VerifyContext& ctx) {
    auto* const window = ctx.window;
    const auto check = [&ctx](bool ok, std::source_location at = std::source_location::current()) {
        if (!ok) std::cerr << "tutorial check failed at line " << at.line() << '\n';
        ctx.check(ok);
    };
    const auto lay_out = [window] {
        QCoreApplication::processEvents();
        (void)window->grabWindow();
    };
    // Long enough for the tour's longest flight (the bubble's 520 ms and the
    // keycap's 700 ms) or Blokkily's 680 ms exit to land.
    const auto land = [&lay_out] {
        VerifyContext::settle(900);
        lay_out();
    };
    const auto press = [&ctx](const char* name) {
        auto* item = ctx.named(QString::fromLatin1(name));
        if (item == nullptr || !item->isVisible()) return false;
        ctx.click_at(item, {item->width() / 2, item->height() / 2}, Qt::LeftButton);
        return true;
    };
    const QRectF frame(0, 0, window->width(), window->height());

    check(ctx.controller.verifyClap(ctx.parser.value("clap-fixture")));
    lay_out();

    auto* const tour = ctx.named("tutorial");
    auto* const help = ctx.named("helpButton");
    check(tour != nullptr);
    if (tour == nullptr) { ctx.finish(" no tutorial in the window"); return; }
    const auto running = [tour] { return tour->property("running").toBool(); };
    const auto step = [tour] { return tour->property("step").toInt(); };
    const auto gone = [&] {
        auto* mascot = ctx.named("tutorialMascot");
        return !running() && !tour->isVisible() && (mascot == nullptr || !mascot->isVisible());
    };

    // ---------------------------------------------------------------------
    ctx.reached("tutorial: HELP sits beside the keyboard hints, whole and on screen");
    check(VerifyContext::usable(help, 22, 20));
    if (help != nullptr && !frame.contains(scene_rect(help))) {
        const auto at = scene_rect(help);
        std::cerr << "HELP at " << at.x() << ".." << at.right() << " of " << frame.width() << '\n';
    }
    check(help != nullptr && frame.contains(scene_rect(help)));
    check(gone());

    // ---------------------------------------------------------------------
    ctx.reached("tutorial: HELP brings Blokkily in with a bubble");
    const int cursor = ctx.pattern.selectedStep();
    const int events = ctx.pattern.property("eventCount").toInt();
    check(press("helpButton"));
    land();
    check(running() && step() == 0 && tour->isVisible());
    check(help != nullptr && help->property("on").toBool());
    auto* const mascot = ctx.named("tutorialMascot");
    auto* const bubble = ctx.named("tutorialBubble");
    auto* const ring = ctx.named("tutorialRing");
    check(VerifyContext::usable(mascot, 72, 72) && mascot->isVisible());
    check(VerifyContext::usable(bubble, 260, 90) && bubble->isVisible());
    check(frame.contains(scene_rect(mascot)) && frame.contains(scene_rect(bubble)));
    // The art is drawn: Blokkily is not an empty box.
    check(mascot != nullptr && mascot->childItems().size() == 1
          && mascot->childItems().first()->childItems().size() >= 400);
    auto* const title = ctx.named("tutorialTitle");
    check(VerifyContext::reads_whole(title, QStringLiteral("HI, I'M BLOKKILY")));
    check(window->activeFocusItem() == tour);
    const QPointF first_bubble = scene_rect(bubble).topLeft();

    // ---------------------------------------------------------------------
    ctx.reached("tutorial: NEXT flies the bubble and the ring to the transport");
    check(press("tutorialNext"));
    land();
    check(step() == 1);
    auto* const play = ctx.named("playButton");
    check(ring != nullptr && ring->isVisible() && scene_rect(ring).contains(scene_rect(play)));
    check(scene_rect(bubble).topLeft() != first_bubble);
    check(!scene_rect(bubble).intersects(scene_rect(play)));
    check(frame.contains(scene_rect(bubble)) && frame.contains(scene_rect(mascot)));
    auto* const keycap = ctx.named("tutorialKeycap");
    check(keycap != nullptr && keycap->isVisible() && frame.contains(scene_rect(keycap)));

    // ---------------------------------------------------------------------
    ctx.reached("tutorial: the window stays playable under the tour");
    check(press("playButton"));
    lay_out();
    check(ctx.transport.playing() && running());
    ctx.controller.togglePlayback();
    lay_out();
    check(!ctx.transport.playing());

    // ---------------------------------------------------------------------
    ctx.reached("tutorial: the arrow keys turn its pages, not the editor's");
    // A key the tour takes is claimed before the window's shortcuts see it.
    for (const auto key : {Qt::Key_Right, Qt::Key_Left, Qt::Key_Return, Qt::Key_Space}) {
        QKeyEvent claim(QEvent::ShortcutOverride, key, Qt::NoModifier);
        claim.ignore();
        QCoreApplication::sendEvent(window, &claim);
        check(claim.isAccepted());
    }
    ctx.type_key(Qt::Key_Right, QString());
    land();
    check(step() == 2);
    ctx.type_key(Qt::Key_Left, QString());
    land();
    check(step() == 1);
    check(ctx.pattern.selectedStep() == cursor);

    // ---------------------------------------------------------------------
    ctx.reached("tutorial: every stop rings its control and keeps the bubble on screen");
    int visited = 2;  // the greeting and the transport, above
    const QString steps_shot = [&] {
        QString path = ctx.parser.value("screenshot");
        return path.isEmpty() ? path : path.left(path.size() - 4) + "-steps.png";
    }();
    for (int index = 2; index < stop_count; ++index) {
        check(press("tutorialNext"));
        land();
        check(step() == index);
        if (step() != index) break;
        ++visited;
        auto* target = ctx.named(QString::fromLatin1(stops[index]));
        check(target != nullptr);
        const QRectF shown = scene_rect(target).intersected(frame);
        check(!shown.isEmpty() && scene_rect(ring).contains(shown));
        check(frame.contains(scene_rect(bubble)) && frame.contains(scene_rect(mascot)));
        if (std::string_view(stops[index]) == "stepGrid")
            check(ctx.save_screenshot_to(steps_shot));
    }
    check(visited == stop_count);
    auto* const next = ctx.named("tutorialNext");
    check(next != nullptr && next->property("text").toString() == "DONE");

    // ---------------------------------------------------------------------
    ctx.reached("tutorial: DONE sends Blokkily away and gives the keyboard back");
    check(press("tutorialNext"));
    land();
    check(gone());
    check(help != nullptr && !help->property("on").toBool());
    check(window->activeFocusItem() != nullptr
          && window->activeFocusItem()->objectName() == "noteEntry");

    // ---------------------------------------------------------------------
    // Every other way out, each from a fresh tour.
    const auto open = [&] {
        check(press("helpButton"));
        land();
        check(running() && tour->isVisible() && step() == 0);
    };
    ctx.reached("tutorial: Escape ends it");
    open();
    ctx.type_key(Qt::Key_Escape, QString(QChar(27)));
    land();
    check(gone());

    ctx.reached("tutorial: X ends it, and is not typed as a note");
    open();
    ctx.type_key(Qt::Key_X, QStringLiteral("x"));
    land();
    check(gone());

    ctx.reached("tutorial: the bubble's close button ends it");
    open();
    check(press("tutorialClose"));
    land();
    check(gone());

    ctx.reached("tutorial: CANCEL ends it");
    open();
    check(press("tutorialNext"));
    land();
    check(press("tutorialCancel"));
    land();
    check(gone());

    ctx.reached("tutorial: HELP again ends it");
    open();
    check(press("helpButton"));
    land();
    check(gone());
    check(ctx.pattern.property("eventCount").toInt() == events);
    check(ctx.pattern.selectedStep() == cursor);

    // ---------------------------------------------------------------------
    // The closing frame is the tour again, at its first stop, for the eye.
    ctx.reached("tutorial: screenshot");
    check(press("helpButton"));
    land();
    check(press("tutorialNext"));
    VerifyContext::settle(1600);
    lay_out();
    check(ctx.save_screenshot());
    ctx.finish(" HELP, every stop, and every way out of the tour");
}

[[maybe_unused]] const bool registered = register_scenario("tutorial", run_tutorial);

}  // namespace
}  // namespace blokkily::verify
