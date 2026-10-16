// features/quantize_humanize.feature, run by bdd_groove: QUANT and HUMAN in
// the pattern header edit the open part, each as one step of history.
#include "verify/harness.hpp"
#include "song_model.hpp"

#include <QCoreApplication>

#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_groove(VerifyContext& ctx) {
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "groove failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    const auto click = [&](QQuickItem* item, Qt::MouseButton button = Qt::LeftButton,
                           Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        if (item) ctx.click_at(item, {item->width() / 2, item->height() / 2}, button, modifiers);
        layout();
    };
    const auto micro_of = [&](int index) { return s.editPattern().events()[static_cast<std::size_t>(index)].micro_offset; };
    layout();
    auto* quantize = ctx.named("quantizeButton");
    auto* humanize = ctx.named("humanizeButton");
    check(VerifyContext::usable(quantize, 50, 16) && VerifyContext::usable(humanize, 30, 16));

    // Play every step of the part a little late.
    const int steps = static_cast<int>(s.editPattern().events().size());
    check(steps >= 2);
    for (int index = 0; index < steps; ++index) {
        ctx.pattern.selectStep(static_cast<int>(s.editPattern().events()[static_cast<std::size_t>(index)].start / 120));
        ctx.pattern.setSelectedMicroOffset(30);
    }
    for (int index = 0; index < steps; ++index) check(micro_of(index) == 30);
    const bool undo_before = s.canUndo();

    // Shift-click: halfway.
    click(quantize, Qt::LeftButton, Qt::ShiftModifier);
    for (int index = 0; index < steps; ++index) check(micro_of(index) == 15);
    // Click: onto the grid.
    click(quantize);
    for (int index = 0; index < steps; ++index) check(micro_of(index) == 0);
    // One undo per click.
    check(undo_before && s.undo());
    for (int index = 0; index < steps; ++index) check(micro_of(index) == 15);
    check(s.redo());
    ctx.reached("groove: QUANT moves the part onto the grid, one undo a click");

    // A right click picks the grid it quantizes to.
    auto* label = quantize ? quantize->childItems().value(0) : nullptr;
    const QString before = label ? label->property("text").toString() : QString();
    click(quantize, Qt::RightButton);
    const QString after = label ? label->property("text").toString() : QString();
    check(before == "QUANT 1/16" && after == "QUANT 1/8");

    // HUMAN loosens timing and velocity, and undoes in one step.
    click(humanize);
    bool loosened = false;
    for (int index = 0; index < steps; ++index) loosened = loosened || micro_of(index) != 0;
    check(loosened);
    check(s.undo());
    for (int index = 0; index < steps; ++index) check(micro_of(index) == 0);
    check(s.redo());
    ctx.reached("groove: HUMAN loosens the part, one undo a click");
    check(ctx.save_screenshot());
    ctx.finish("quantize and humanize the open part");
}

[[maybe_unused]] const bool registered = register_scenario("groove", run_groove);

} // namespace
} // namespace blokkily::verify
