// features/editors_are_inputs.feature, run by bdd_roll_zoom: the piano roll
// zooms - the steps wider (and a note drawn there lands on the step under the
// pointer), the pitch window narrower or wider - and its VELOCITY lane sets
// each note's velocity with one drag, one step of history.
#include "verify/harness.hpp"

#include <QCoreApplication>

#include <cmath>
#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_roll_zoom(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "roll zoom failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    const auto click = [&](const char* name) {
        auto* item = ctx.named(QString::fromLatin1(name));
        if (item == nullptr || !item->isVisible() || !item->isEnabled()) return false;
        ctx.click_at(item, {item->width() / 2, item->height() / 2}, Qt::LeftButton);
        layout();
        return true;
    };
    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    check(ctx.window->setProperty("view", "PIANO"));
    song.selectTrack(0);
    for (int step = 0; step < pattern.stepCount(); ++step) pattern.clearStep(step);
    for (int step = 0; step < 16; step += 4) pattern.setStepKey(step, 60);
    layout();
    auto* input = ctx.named("rollInput");
    check(VerifyContext::usable(input, 200, 100));
    // The roll's lane width, read from where step 4's note is drawn.
    const auto lane_width = [&] {
        auto* first = ctx.named("rollNote0");
        auto* second = ctx.named("rollNote4");
        return first && second ? (second->x() - first->x()) / 4.0 : 0.0;
    };
    const double fitted = lane_width();
    for (const char* name : {"rollZoomIn", "rollZoomOut", "rollSpanIn", "rollSpanOut"})
        check(VerifyContext::usable(ctx.named(QString::fromLatin1(name)), 20, 16));
    check(click("rollZoomIn") && click("rollZoomIn"));
    const double zoomed = lane_width();
    std::cerr << "roll zoom: a step " << fitted << " px, zoomed " << zoomed << " px\n";
    check(fitted > 0 && std::abs(zoomed - 4 * fitted) < 0.5);
    check(ctx.named("rollScroll") && ctx.named("rollScroll")->isVisible());
    // Drawn in the zoomed roll: the pointer over a step in view writes that
    // step. The zoom kept the middle in view, so the roll has scrolled.
    int target = -1;
    if (input) {
        auto* first = ctx.named("rollNote0");
        const double scroll = first ? input->x() + 1 - first->x() : 0.0;
        target = static_cast<int>(std::ceil(scroll / zoomed)) + 1;
        if (target % 4 == 0) ++target;
        const double lane_height = input->height() / (pattern.highKey() - pattern.lowKey() + 1);
        ctx.click_at(input, {(target + 0.5) * zoomed - scroll, (pattern.highKey() - 64 + 0.5) * lane_height},
                     Qt::LeftButton);
        layout();
    }
    std::cerr << "roll zoom: drew at step " << target << '\n';
    check(target >= 0 && pattern.hasStep(target) && pattern.stepKey(target) == 64);
    // Kept out of the velocity drag below.
    if (target >= 0) pattern.clearStep(target);
    check(click("rollZoomOut") && click("rollZoomOut"));
    check(std::abs(lane_width() - fitted) < 0.5 && !ctx.named("rollZoomOut")->isEnabled());
    // The pitch window: narrower makes each lane taller, and wider again.
    const int span = pattern.highKey() - pattern.lowKey();
    check(click("rollSpanIn"));
    const int narrower = pattern.highKey() - pattern.lowKey();
    check(click("rollSpanOut") && click("rollSpanOut"));
    const int wider = pattern.highKey() - pattern.lowKey();
    std::cerr << "roll zoom: " << span << " keys, " << narrower << " narrower, " << wider << " wider\n";
    check(narrower == span - 6 && wider == span + 6);
    ctx.reached("roll zoom: the steps widen and the pitch window narrows and widens");

    // VELOCITY: a drag rising across the four beats.
    auto* picker = ctx.named("controllerLanePicker");
    check(picker != nullptr);
    if (picker) {
        ctx.click_at(picker, {picker->width() / 2, picker->height() / 2}, Qt::LeftButton);
        layout();
        auto* menu = picker->findChild<QObject*>("controllerLanePickerMenu");
        // The entry that reads VELOCITY, wherever the menu lists it.
        QQuickItem* entry = nullptr;
        const int count = menu ? menu->property("count").toInt() : 0;
        for (int row = 0; row < count && entry == nullptr; ++row) {
            QQuickItem* candidate = nullptr;
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, candidate), Q_ARG(int, row));
            if (candidate && candidate->property("text").toString() == "VELOCITY") entry = candidate;
        }
        check(entry != nullptr);
        if (entry) ctx.click_at(entry, {entry->width() / 2, entry->height() / 2}, Qt::LeftButton);
        layout();
    }
    auto* lane = ctx.named("controllerLaneInput");
    check(VerifyContext::usable(lane, 200, 30));
    if (lane) {
        const double step_px = lane_width();
        const double h = lane->height();
        const auto at = [&](int step, double level) { return QPointF((step + 0.5) * step_px, 2 + (1 - level) * (h - 4)); };
        ctx.mouse_at(lane, at(0, 0.2), QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        for (int step = 1; step <= 12; ++step) {
            ctx.mouse_at(lane, at(step, 0.2 + 0.05 * step), QEvent::MouseMove, Qt::NoButton, Qt::LeftButton);
            layout();
        }
        ctx.mouse_at(lane, at(12, 0.8), QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        layout();
    }
    double previous = -1.0;
    bool rising = true;
    int notes = 0;
    for (const auto& item : pattern.noteVelocities()) {
        const auto note = item.toMap();
        if (note.value("step").toInt() % 4 != 0) continue;
        const double level = note.value("level").toDouble();
        std::cerr << "velocity at step " << note.value("step").toInt() << ": " << level << '\n';
        rising = rising && level > previous;
        previous = level;
        ++notes;
    }
    check(notes == 4 && rising && std::abs(previous - 0.8) < 0.05);
    check(ctx.named("controllerPoint0") != nullptr);
    // One drag, one step of history.
    check(song.undo());
    layout();
    double undone = 0.0;
    for (const auto& item : pattern.noteVelocities())
        if (item.toMap().value("step").toInt() == 12) undone = item.toMap().value("level").toDouble();
    check(std::abs(undone - 0.8) > 0.05);
    ctx.reached("roll zoom: the VELOCITY lane sets the notes' velocities in one step of history");
    check(ctx.save_screenshot());
    ctx.finish("the piano roll zooms, and its velocity lane");
}

[[maybe_unused]] const bool registered = register_scenario("roll_zoom", run_roll_zoom);

} // namespace
} // namespace blokkily::verify
