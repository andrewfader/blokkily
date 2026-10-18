// features/window_layout.feature, run by bdd_window_layout with --auto-hide
// (the side bars as they ship): the bars tuck away at 1280 px and come out
// when the pointer rests on their strip, stay while used, and go when not;
// keys and the tour bring them out; a roomy window or auto-hide off docks
// them; the editors' panels move by their grips and the keyboard is sized by
// the gap under it; a bar widens by its inner edge; RESET puts it all back.
// Everything is driven through pointer and key events at the real window.
#include "verify/harness.hpp"

#include <QCoreApplication>

#include <cmath>
#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_window_layout(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto* const window = ctx.window;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "window layout failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)window->grabWindow(); };
    const auto settle = [&](int ms) { VerifyContext::settle(ms); layout(); };
    const auto centre = [](QQuickItem* item) {
        return item ? QPointF(item->width() / 2, item->height() / 2) : QPointF();
    };
    // The pointer moves to the middle of `item`, no button held.
    const auto hover = [&](QQuickItem* item) {
        ctx.mouse_at(item, centre(item), QEvent::MouseMove, Qt::NoButton, Qt::NoButton);
        layout();
    };
    // Presses at the middle of `item`, moves by `delta` in steps, lets go.
    // The points are fixed in the window when the press lands: the handle
    // dragged moves with what it sizes.
    const auto drag = [&](QQuickItem* item, QPointF delta) {
        if (item == nullptr) return;
        auto* scene = window->contentItem();
        const QPointF from = item->mapToScene(centre(item));
        ctx.mouse_at(scene, from, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        for (int step = 1; step <= 10; ++step) {
            ctx.mouse_at(scene, from + delta * (step / 10.0), QEvent::MouseMove, Qt::NoButton,
                         Qt::LeftButton);
            layout();
        }
        ctx.mouse_at(scene, from + delta, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        layout();
    };
    const auto click = [&](const char* name) {
        auto* item = ctx.named(QString::fromLatin1(name));
        if (item == nullptr || !item->isVisible()) return false;
        ctx.click_at(item, centre(item), Qt::LeftButton);
        layout();
        return true;
    };
    const auto flag = [](QQuickItem* item, const char* name) {
        return item != nullptr && item->property(name).toBool();
    };
    const auto scene_x = [](QQuickItem* item) { return item ? item->mapToScene({0, 0}).x() : -1.0; };

    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    check(window->setProperty("view", "ALL"));
    settle(50);
    auto* rail = ctx.named("rail");
    auto* mixer = ctx.named("mixer");
    auto* rail_panel = ctx.named("railDrawer");
    auto* mixer_panel = ctx.named("mixerDrawer");
    auto* rail_strip = ctx.named("railStrip");
    auto* mixer_strip = ctx.named("mixerStrip");
    auto* editors = ctx.named("editorArea");
    check(rail && mixer && rail_panel && mixer_panel && rail_strip && mixer_strip && editors);
    if (!(rail && mixer && rail_panel && mixer_panel && rail_strip && mixer_strip && editors)) {
        ctx.finish("the window's bars were not found");
        return;
    }

    // ------------------------------------------------ tucked away at first
    check(!flag(rail, "docked") && !flag(mixer, "docked"));
    check(!rail_panel->isVisible() && !mixer_panel->isVisible());
    check(rail->width() == 14 && mixer->width() == 14 && rail_strip->isVisible() && mixer_strip->isVisible());
    const double editors_x = scene_x(editors);
    std::cerr << "editors " << editors_x << " +" << editors->width() << '\n';
    check(editors_x == 14 && editors->width() == 1280 - 28);
    ctx.reached("window layout: the bars are strips at 1280 px, the editors have the room");

    // ------------------------------------ resting on the edge brings it out
    hover(rail_strip);
    settle(450);
    check(flag(rail, "revealed") && rail_panel->isVisible());
    check(VerifyContext::usable(rail_panel, 200, 600) && std::abs(scene_x(rail_panel)) < 0.5);
    check(VerifyContext::usable(ctx.named("instrumentPanel"), 180, 200));
    // Over the editors, not moving them.
    check(scene_x(editors) == editors_x && editors->width() == 1280 - 28);
    if (ctx.parser.isSet("screenshot")) {
        QString path = ctx.parser.value("screenshot");
        check(ctx.save_screenshot_to(path.left(path.size() - 4) + QStringLiteral("-rail.png")));
    }
    hover(rail_panel);
    settle(900);
    check(flag(rail, "revealed"));
    // Typing a search holds it out with the pointer gone.
    check(click("pluginFilter"));
    auto* filter = ctx.named("pluginFilter");
    check(filter && filter->hasActiveFocus());
    hover(editors);
    settle(900);
    check(flag(rail, "revealed") && rail->property("revealed").toBool());
    ctx.reached("window layout: the left bar is out while pointed at and while its search is typed in");
    // Escape hands the keyboard back; with nothing to hold it, it goes.
    ctx.type_key(Qt::Key_Escape, QString());
    settle(1000);
    check(!flag(rail, "revealed") && !rail_panel->isVisible());
    // Passing over the strip on the way somewhere does not bring it out.
    hover(rail_strip);
    VerifyContext::settle(60);
    hover(editors);
    settle(450);
    check(!flag(rail, "revealed"));
    ctx.reached("window layout: leaving sends it back, and passing over does not bring it out");

    // ------------------------------------------ keys and the tour bring them
    ctx.chord_key(Qt::Key_BracketRight, Qt::ControlModifier);
    settle(900);
    check(flag(mixer, "revealed") && VerifyContext::usable(mixer_panel, 200, 600));
    check(std::abs(scene_x(mixer_panel) + mixer_panel->width() - 1280) < 0.5);
    hover(mixer_panel);
    hover(editors);
    settle(1000);
    check(!flag(mixer, "revealed"));
    ctx.chord_key(Qt::Key_F, Qt::ControlModifier);
    settle(300);
    check(flag(rail, "revealed") && filter && filter->hasActiveFocus());
    ctx.type_key(Qt::Key_Escape, QString());
    settle(1000);
    check(!flag(rail, "revealed"));
    auto* tutorial = ctx.named("tutorial");
    check(tutorial != nullptr);
    if (tutorial) QMetaObject::invokeMethod(tutorial, "start");
    settle(400);
    check(flag(rail, "revealed") && flag(mixer, "revealed"));
    if (tutorial) QMetaObject::invokeMethod(tutorial, "finish");
    settle(1100);
    check(!flag(rail, "revealed") && !flag(mixer, "revealed"));
    ctx.reached("window layout: Ctrl+], Ctrl+F and the tour bring the bars out");

    // ----------------------------------------------- room docks the bars
    window->resize(1700, 1080);
    settle(100);
    check(flag(rail, "docked") && flag(mixer, "docked"));
    std::cerr << "wide editors " << scene_x(editors) << " +" << editors->width() << '\n';
    check(scene_x(editors) == 232 && editors->width() == 1700 - 232 - 250);
    window->resize(1280, 1080);
    settle(100);
    check(!flag(rail, "docked") && !flag(mixer, "docked"));
    check(click("viewSETTINGS"));
    check(click("autoHideRail"));
    check(flag(rail, "docked") && !flag(mixer, "docked") && rail->width() == 232);
    check(scene_x(editors) == 232);
    check(c.uiSettings().value("ui/autoHideRail").toBool() == false);
    check(click("viewALL"));
    ctx.reached("window layout: a roomy window, or auto-hide off, docks the bars");

    // ---------------------------------------------------- panels move
    auto* arrangement = ctx.named("arrangementView");
    auto* keyboard = ctx.named("keyboardPanel");
    auto* grip = ctx.named("grip_keyboard");
    check(arrangement && keyboard && grip && grip->isVisible());
    check(VerifyContext::usable(grip, 8, 16));
    if (arrangement && keyboard && grip) {
        check(keyboard->y() > arrangement->y());
        // Into view, then up to the arrangement's top.
        auto* scroll = ctx.named("editorScroll");
        if (scroll) scroll->setProperty("contentY", std::max(0.0, keyboard->y() - 200));
        layout();
        const QPointF target = grip->mapFromItem(arrangement, QPointF(0, 4));
        drag(grip, QPointF(0, target.y() - grip->height() / 2));
        if (scroll) scroll->setProperty("contentY", 0);
        settle(50);
        std::cerr << "order: " << c.uiSettings().value("ui/editorOrder").toString().toStdString()
                  << " keyboard y " << keyboard->y() << " arrangement y " << arrangement->y() << '\n';
        check(c.uiSettings().value("ui/editorOrder").toString().startsWith("keyboard,arrangement"));
        check(keyboard->y() < arrangement->y());
    }
    ctx.reached("window layout: the keyboard's grip moves it above the arrangement");

    // ---------------------------------------------------- the keyboard sizes
    auto* sizer = ctx.named("sizer_keyboard");
    check(sizer && sizer->isVisible() && VerifyContext::usable(sizer, 200, 8));
    if (keyboard && sizer) {
        const double natural = keyboard->height();
        drag(sizer, QPointF(0, 60));
        settle(50);
        std::cerr << "keyboard " << natural << " -> " << keyboard->height() << '\n';
        check(std::abs(keyboard->height() - (natural + 60)) <= 2.0);
        check(std::abs(c.uiSettings().value("ui/keyboardHeight").toDouble() - (natural + 60)) <= 2.0);
        // A double-click gives back the height its keys want.
        ctx.click_at(sizer, centre(sizer), Qt::LeftButton);
        ctx.mouse_at(sizer, centre(sizer), QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        ctx.mouse_at(sizer, centre(sizer), QEvent::MouseButtonDblClick, Qt::LeftButton, Qt::LeftButton);
        ctx.mouse_at(sizer, centre(sizer), QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        settle(50);
        check(std::abs(keyboard->height() - natural) <= 0.5);
        check(!c.uiSettings().contains("ui/keyboardHeight"));
    }
    ctx.reached("window layout: the gap under the keyboard sizes it, a double-click resets it");

    // ------------------------------------------------- a bar widens
    check(click("viewSETTINGS"));
    check(click("autoHideMixer"));
    check(flag(mixer, "docked") && mixer->width() == 250);
    auto* resizer = ctx.named("mixerResizer");
    check(resizer && resizer->isVisible());
    drag(resizer, QPointF(-50, 0));
    settle(50);
    std::cerr << "mixer " << mixer->width() << '\n';
    check(mixer->width() == 300 && c.uiSettings().value("ui/mixerWidth").toInt() == 300);
    check(VerifyContext::usable(ctx.named("mixerPanel"), 290, 600));
    ctx.reached("window layout: the mixer's inner edge widens it, remembered");

    // ------------------------------------------------------- reset
    check(click("resetLayout"));
    check(click("viewALL"));
    settle(50);
    check(c.uiSettings().isEmpty());
    check(!flag(rail, "docked") && !flag(mixer, "docked"));
    check(mixer->property("storedWidth").toDouble() == 250);
    check(arrangement && keyboard && keyboard->y() > arrangement->y());
    ctx.reached("window layout: RESET puts the panels and the bars back");

    check(ctx.save_screenshot());
    ctx.finish("auto-hiding bars, movable and sizable panels");
}

[[maybe_unused]] const bool registered = register_scenario("window_layout", run_window_layout);

} // namespace
} // namespace blokkily::verify
