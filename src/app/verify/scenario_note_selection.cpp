// features/note_selection.feature, run by bdd_note_selection: several notes
// are chosen in the rendered piano roll with Shift - a click each, or a band
// dragged across them - and moved by dragging one of them, deleted, copied
// and pasted, and transposed, each as one step of history. Everything is
// driven through pointer and key events at the real window.
#include "verify/harness.hpp"

#include "pattern_model.hpp"
#include "song_model.hpp"

#include <QCoreApplication>

#include <iostream>
#include <set>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_note_selection(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    auto* const window = ctx.window;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "note selection failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)window->grabWindow(); };
    // The notes of the open part, as (step, key).
    const auto notes = [&] {
        std::set<std::pair<int, int>> found;
        for (int step = 0; step < pattern.stepCount(); ++step)
            for (int key = 0; key < 128; ++key)
                if (pattern.rollNoteDuration(step, key) > 0) found.insert({step, key});
        return found;
    };

    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    check(window->setProperty("view", "PIANO"));
    song.selectTrack(0);
    for (int step = 0; step < pattern.stepCount(); ++step) pattern.clearStep(step);
    for (const auto& [step, key] : {std::pair{0, 60}, {4, 64}, {8, 67}, {12, 72}})
        pattern.setStepKey(step, key);
    pattern.clearNoteSelection();
    layout();
    const auto written = notes();
    check(written.size() == 4);

    auto* input = ctx.named("rollInput");
    auto* area = input != nullptr ? input->parentItem() : nullptr;
    check(input != nullptr && area != nullptr && VerifyContext::usable(input, 300, 150));
    if (input == nullptr || area == nullptr) {
        ctx.finish("the piano roll was not found");
        return;
    }
    // The middle of a note's lane, a little into its step, in the roll's input.
    const auto at = [&](double step, int key) {
        const double lane_width = area->property("laneWidth").toDouble();
        const double lane_height = area->property("laneHeight").toDouble();
        const double scroll = area->property("scrollX").toDouble();
        const int high = pattern.property("highKey").toInt();
        return QPointF(step * lane_width - scroll + lane_width * 0.3, (high - key + 0.5) * lane_height);
    };
    const auto press = [&](QPointF point, Qt::KeyboardModifiers held) {
        ctx.mouse_at(input, point, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton, held);
    };
    const auto move = [&](QPointF point, Qt::KeyboardModifiers held) {
        ctx.mouse_at(input, point, QEvent::MouseMove, Qt::NoButton, Qt::LeftButton, held);
    };
    const auto release = [&](QPointF point, Qt::KeyboardModifiers held) {
        ctx.mouse_at(input, point, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton, held);
        layout();
    };

    // ----------------------------------------------- Shift-click chooses
    for (const auto& [step, key] : {std::pair{4, 64}, {8, 67}}) {
        press(at(step, key), Qt::ShiftModifier);
        release(at(step, key), Qt::ShiftModifier);
    }
    check(pattern.noteSelectionCount() == 2 && pattern.isNoteSelected(4, 64) && pattern.isNoteSelected(8, 67));
    check(notes() == written);
    ctx.reached("note selection: Shift-clicks choose two notes and change none");

    // ------------------------------------ dragging one moves them both
    {
        const QPointF from = at(4, 64);
        const QPointF to = at(5, 66);
        press(from, Qt::NoModifier);
        for (int step = 1; step <= 6; ++step) move(from + (to - from) * (step / 6.0), Qt::NoModifier);
        release(to, Qt::NoModifier);
        const std::set<std::pair<int, int>> moved{{0, 60}, {5, 66}, {9, 69}, {12, 72}};
        std::cerr << "after the drag:";
        for (const auto& [step, key] : notes()) std::cerr << ' ' << step << ':' << key;
        std::cerr << '\n';
        check(notes() == moved && pattern.isNoteSelected(5, 66) && pattern.isNoteSelected(9, 69));
        if (ctx.parser.isSet("screenshot")) {
            QString path = ctx.parser.value("screenshot");
            check(ctx.save_screenshot_to(path.left(path.size() - 4) + QStringLiteral("-chosen.png")));
        }
        check(song.undo());
        layout();
        check(notes() == written);
    }
    ctx.reached("note selection: a chosen note dragged moves every chosen note, one undo puts them back");

    // ---------------------------------------- a band chooses, Delete removes
    {
        const QPointF from = at(-0.2 + 0.0, 70) + QPointF(0, 0);
        const QPointF to = at(8.6, 59);
        press(QPointF(std::max(1.0, from.x()), from.y()), Qt::ShiftModifier);
        for (int step = 1; step <= 6; ++step)
            move(QPointF(std::max(1.0, from.x()), from.y()) + (to - QPointF(std::max(1.0, from.x()), from.y())) * (step / 6.0),
                 Qt::ShiftModifier);
        auto* band = ctx.named("rollBand");
        check(band != nullptr && band->isVisible() && band->width() > 20 && band->height() > 20);
        release(to, Qt::ShiftModifier);
        std::cerr << "the band chose " << pattern.noteSelectionCount() << '\n';
        check(pattern.noteSelectionCount() == 3 && pattern.isNoteSelected(0, 60) &&
              pattern.isNoteSelected(4, 64) && pattern.isNoteSelected(8, 67) && !pattern.isNoteSelected(12, 72));
        ctx.type_key(Qt::Key_Delete, QString());
        layout();
        check(notes() == std::set<std::pair<int, int>>{{12, 72}});
        check(song.undo());
        layout();
        check(notes() == written);
    }
    ctx.reached("note selection: a Shift band chooses the notes it covers, Delete removes just those");

    // ----------------------------------- every note, copied and pasted
    {
        ctx.chord_key(Qt::Key_A, Qt::ControlModifier);
        layout();
        check(pattern.noteSelectionCount() == 4);
        ctx.chord_key(Qt::Key_C, Qt::ControlModifier);
        check(pattern.notesCopied());
        pattern.selectStep(2);
        ctx.chord_key(Qt::Key_V, Qt::ControlModifier);
        layout();
        auto pasted = written;
        for (const auto& note : std::set<std::pair<int, int>>{{2, 60}, {6, 64}, {10, 67}, {14, 72}}) pasted.insert(note);
        check(notes() == pasted && pattern.noteSelectionCount() == 4 && pattern.isNoteSelected(14, 72));
        // The pasted notes are chosen: Shift+Up takes them up together.
        ctx.chord_key(Qt::Key_Up, Qt::ShiftModifier);
        layout();
        check(pattern.rollNoteDuration(2, 61) > 0 && pattern.rollNoteDuration(14, 73) > 0 &&
              pattern.rollNoteDuration(0, 60) > 0);
        check(song.undo() && song.undo());
        layout();
        check(notes() == written);
    }
    ctx.reached("note selection: Ctrl+A, Ctrl+C and Ctrl+V copy every note, Shift+Up transposes those chosen");

    // A step chosen elsewhere lets the notes go, so Delete clears just that step.
    pattern.selectAllNotes();
    pattern.selectStep(3);
    check(pattern.noteSelectionCount() == 0);
    ctx.reached("note selection: moving the cursor elsewhere lets the chosen notes go");

    check(ctx.save_screenshot());
    ctx.finish("several notes chosen in the piano roll, moved, deleted, copied and transposed together");
}

[[maybe_unused]] const bool registered = register_scenario("note_selection", run_note_selection);

} // namespace
} // namespace blokkily::verify
