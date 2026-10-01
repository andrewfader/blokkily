// features/per_stem_editing.feature, run on its own by the bdd_per_stem_editing
// gate (`--scenario stems`). A song is built from two stems, each the CLAP
// fixture on its own track, and one pattern (a section, with a part for each
// stem) placed at bar 1. The scenario selects stems from the rendered mixer
// and the rendered stem rack, draws in the rendered piano roll and rack,
// presses the rendered arrangement, and proves through the song model and the
// production render callback that: selecting a stem keeps the pattern open
// and shows that stem's own part of it; notes drawn on one stem stay on it and
// are heard on its instrument; a new stem can be written to at once; and
// placing, removing and opening clips in the lanes act on the right stems.

#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointF>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <utility>

namespace blokkily::verify {
namespace {

// Whether a pattern carries a note that starts on `step` (a sixteenth).
bool note_on_step(const blokkily::Pattern& pattern, int step) {
    for (const auto& trigger : pattern.events())
        if (static_cast<int>(trigger.start / PatternModel::ticks_per_step) == step) return true;
    return false;
}

std::size_t note_count(const blokkily::Pattern& pattern) { return pattern.events().size(); }

void run_stems(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    auto& controller = ctx.controller;
    const auto check = [&ctx](bool ok) { ctx.check(ok); };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };

    // --- The session: the CLAP fixture on two stems, and one pattern, VERSE,
    // placed at bar 1, so each stem plays its part of it there. -------------
    check(controller.verifyClap(ctx.parser.value("clap-fixture")));
    check(controller.engine() != nullptr &&
          song.song().tracks.at(0).instrument.format == "CLAP");
    controller.setTempo(120.0);
    {
        auto configured = song.song();
        const auto synth = configured.tracks.at(0).instrument;
        configured.tracks.assign(2, blokkily::Track{});
        configured.tracks[0].name = "STEM A";
        configured.tracks[0].instrument = synth;
        configured.tracks[1].name = "STEM B";
        configured.tracks[1].instrument = synth;
        configured.patterns = {{"VERSE", blokkily::Pattern(1920, 480)}};
        configured.clips = {{0, 0, 0, 1}};
        configured.sections.clear();
        configured.launcher = blokkily::SceneMatrix{};
        configured.modulators.clear();
        song.replace(std::move(configured));
        controller.flushRecompile();
    }
    check(window->setProperty("view", "ALL"));
    VerifyContext::settle(40);
    lay_out();
    // VERSE is one section with a part for each stem, both placed at bar 1.
    const int verse_a = song.partOf(0, 0);
    const int verse_b = song.partOf(0, 1);
    check(song.song().tracks.size() == 2 && song.sections().size() == 1 && verse_a >= 0 &&
          verse_b >= 0 && verse_a != verse_b && song.hasClip(0, 0) && song.hasClip(1, 0));
    check(controller.engine() != nullptr && controller.engine()->has_instrument(0) &&
          controller.engine()->has_instrument(1));
    reached("stems: a song built from two stems and one pattern");

    const auto part_of = [&](int part) -> const blokkily::Pattern& {
        return song.song().patterns.at(static_cast<std::size_t>(std::max(0, part))).pattern;
    };
    // Select a stem by pressing its rendered mixer strip, the way a producer
    // does, and draw onto the rendered piano roll at a given sixteenth.
    const auto select_stem = [&](int index) {
        // A track added or a clip placed rebuilds the strips; lay them out
        // before pressing, or a strip not yet sized takes no press.
        lay_out();
        auto* strip = ctx.named(QString("mixerStrip%1").arg(index));
        check(VerifyContext::usable(strip, 40, 60));
        if (strip != nullptr)
            ctx.click_at(strip, {strip->width() / 2.0, 16.0}, Qt::LeftButton);
        lay_out();
    };
    const auto draw_step = [&](int step) {
        auto* roll = ctx.named("rollInput");
        check(roll != nullptr && roll->width() > 0 && roll->height() > 0);
        if (roll == nullptr) return;
        const int high = pattern.highKey();
        const int lanes = std::max(1, high - pattern.lowKey() + 1);
        const double lane_height = roll->height() / lanes;
        const double lane_width = roll->width() / std::max(1, pattern.stepCount());
        const int key = high - 3;
        ctx.click_at(roll, QPointF((step + 0.5) * lane_width, (high - key + 0.5) * lane_height),
                     Qt::LeftButton);
        lay_out();
    };
    // How loud each track is over one whole pass of bar 1. A 4/4 bar at 120
    // BPM is ~96000 samples; 110 blocks of 1024 cover it, so a note on any
    // step of the bar is caught whichever step it sits on.
    const auto track_peaks = [&] {
        controller.flushRecompile();
        controller.seekToStep(0);
        controller.togglePlayback();
        std::vector<float> peaks(static_cast<std::size_t>(song.trackCount()), 0.0F);
        for (int block = 0; block < 110; ++block) {
            (void)ctx.pump();
            for (std::size_t track = 0; track < peaks.size(); ++track)
                peaks[track] = std::max(peaks[track], controller.engine()->track_peak(track));
        }
        controller.togglePlayback();
        (void)ctx.pump();
        return peaks;
    };

    // --- Selecting a stem shows its own part of the open pattern. -----------
    select_stem(0);
    check(song.selectedTrack() == 0 && song.currentSection() == 0 &&
          song.currentPattern() == verse_a);
    draw_step(2);
    draw_step(6);
    draw_step(10);
    check(note_count(part_of(verse_a)) == 3 && note_on_step(part_of(verse_a), 2) &&
          note_on_step(part_of(verse_a), 6) && note_on_step(part_of(verse_a), 10));
    select_stem(1);
    // The pattern stays open; the editors now show STEM B's part, empty.
    check(song.selectedTrack() == 1 && song.currentSection() == 0 &&
          song.currentPattern() == verse_b && pattern.rowCount() == 0);
    auto* title = ctx.named("patternTitle");
    auto* stem = ctx.named("patternStem");
    check(title != nullptr && title->property("text").toString() == "VERSE" && stem != nullptr &&
          stem->property("text").toString().contains("STEM B"));
    reached("stems: selecting a stem keeps the pattern open and shows that stem's part");

    // --- Notes added to a stem stay on that stem and are heard on it. -------
    {
        const auto peaks = track_peaks();
        check(peaks.at(0) > 1e-3F && peaks.at(1) < 1e-4F);
    }
    draw_step(4);
    draw_step(8);
    const bool second_kept = note_count(part_of(verse_b)) == 2 &&
                             note_on_step(part_of(verse_b), 4) &&
                             note_on_step(part_of(verse_b), 8);
    const bool first_intact = note_count(part_of(verse_a)) == 3;
    check(second_kept && first_intact);
    if (!second_kept || !first_intact)
        std::cerr << "REGRESSION: drawing on the second stem did not keep both stems' notes "
                     "independent (A notes="
                  << note_count(part_of(verse_a)) << " B notes=" << note_count(part_of(verse_b))
                  << ")\n";
    {
        const auto peaks = track_peaks();
        check(peaks.at(0) > 1e-3F && peaks.at(1) > 1e-3F);
    }
    reached("stems: notes drawn on each stem stay on it and are heard on it");

    // --- The stem rack shows every stem's steps and edits any of them. ------
    {
        auto* rack = ctx.named("stemRack");
        check(VerifyContext::usable(rack, 300, 30));
        const auto rows = pattern.stemRows();
        check(rows.size() == 2);
        if (rows.size() == 2) {
            const auto a = rows.at(0).toMap().value("steps").toList();
            const auto b = rows.at(1).toMap().value("steps").toList();
            check(a.size() == 16 && a.at(2).toBool() && a.at(6).toBool() && !a.at(4).toBool() &&
                  b.size() == 16 && b.at(4).toBool() && !b.at(2).toBool());
        }
        // A rendered rack step on STEM A's row toggles STEM A's part and
        // selects STEM A; the rows are rebuilt, so each is looked up afresh.
        auto* cell = VerifyContext::child_named(rack, "stemStep0-12");
        check(VerifyContext::usable(cell, 8, 8));
        if (cell != nullptr)
            ctx.click_at(cell, {cell->width() / 2.0, cell->height() / 2.0}, Qt::LeftButton);
        lay_out();
        check(song.selectedTrack() == 0 && note_on_step(part_of(verse_a), 12) &&
              note_count(part_of(verse_a)) == 4 && note_count(part_of(verse_b)) == 2);
        // Its name selects a stem without touching any notes.
        auto* name = VerifyContext::child_named(ctx.named("stemRack"), "stemName1");
        check(VerifyContext::usable(name, 40, 10));
        if (name != nullptr)
            ctx.click_at(name, {name->width() / 2.0, name->height() / 2.0}, Qt::LeftButton);
        lay_out();
        check(song.selectedTrack() == 1 && song.currentPattern() == verse_b &&
              note_count(part_of(verse_a)) == 4 && note_count(part_of(verse_b)) == 2);
    }
    reached("stems: the stem rack shows every stem and toggles any stem's step");

    // --- A new stem can be written to straight away. ------------------------
    const auto patterns_before = song.song().patterns.size();
    // The General MIDI bank it is given is the gate's own fixture, so the
    // result does not hang on which banks this machine has installed.
    controller.addTrack(
        {std::filesystem::path(ctx.parser.value("soundfont-fixture").toStdString()).parent_path()});
    const int fresh = song.selectedTrack();
    const int verse_fresh = song.partOf(0, fresh);
    // It has an empty part of VERSE, placed where VERSE plays.
    check(fresh == 2 && song.song().patterns.size() == patterns_before + 1 &&
          verse_fresh >= 0 && song.currentPattern() == verse_fresh &&
          note_count(part_of(verse_fresh)) == 0 && song.hasClip(fresh, 0));
    lay_out();
    draw_step(5);
    check(note_count(part_of(verse_fresh)) == 1 && note_on_step(part_of(verse_fresh), 5));
    check(note_count(part_of(verse_a)) == 4 && note_count(part_of(verse_b)) == 2);
    {
        const auto peaks = track_peaks();
        check(controller.engine()->has_instrument(static_cast<std::size_t>(fresh)) &&
              peaks.size() == 3 && peaks.at(2) > 1e-3F);
    }
    // One undo takes back the note alone; the new stem keeps its part.
    check(song.undo() && note_count(part_of(verse_fresh)) == 0 && song.trackCount() == 3);
    check(song.redo() && note_count(part_of(verse_fresh)) == 1);
    reached("stems: a new stem is written to at once and heard");

    // --- The devices card names the selected stem's own instrument. ---------
    {
        const auto shown = [&] {
            auto* label = ctx.named("selectedInstrument");
            return label == nullptr ? QString() : label->property("text").toString();
        };
        const auto carried = [&](int track) {
            return song.tracks().at(track).toMap().value("instrument").toString();
        };
        select_stem(fresh);
        const bool fresh_named = shown() == carried(fresh);
        select_stem(1);
        const bool stem_named = shown() == carried(1);
        check(carried(fresh) != carried(1) && fresh_named && stem_named);
        if (!fresh_named || !stem_named)
            std::cerr << "REGRESSION: the devices card shows \"" << shown().toStdString()
                      << "\" for a stem carrying \"" << carried(1).toStdString() << "\"\n";
    }
    reached("stems: the devices card names the selected stem's instrument");

    // --- Pressing the lanes. -----------------------------------------------
    // The cell of a lane, looked up afresh because the rows are rebuilt.
    const auto cell_at = [&](int track, int bar) -> QQuickItem* {
        auto* row = VerifyContext::child_named(ctx.named("arrangementRows"),
                                               QString("arrangeRow%1").arg(track));
        return VerifyContext::child_named(row, QString("clip%1-%2").arg(track).arg(bar));
    };
    const auto press = [&](int track, int bar, Qt::MouseButton button,
                           Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        lay_out();
        auto* cell = cell_at(track, bar);
        check(VerifyContext::usable(cell, 8, 8));
        if (cell != nullptr)
            ctx.click_at(cell, {cell->width() / 2.0, cell->height() / 2.0}, button, modifiers);
        lay_out();
    };
    if (auto* scroll = ctx.named("editorScroll")) scroll->setProperty("contentY", 0);
    // A new pattern, CHORUS, opened. Pressing an empty bar on STEM B's lane,
    // with STEM A selected, places CHORUS on every stem at that bar.
    song.addPattern(1);
    const int chorus = song.currentSection();
    song.renamePattern(chorus, "CHORUS");
    select_stem(0);
    check(chorus == 1 && song.currentSection() == chorus);
    press(1, 1, Qt::LeftButton);
    bool everywhere = song.selectedTrack() == 1;
    for (int track = 0; track < 3; ++track) {
        const auto* placed = cell_at(track, 1);
        everywhere = everywhere && placed != nullptr && placed->property("filled").toBool() &&
                     song.lanes().at(track).toList().at(1).toMap().value("pattern").toInt() ==
                         song.partOf(chorus, track);
    }
    check(everywhere);
    if (!everywhere)
        std::cerr << "REGRESSION: pressing an empty bar did not place the open pattern on "
                     "every stem\n";
    // The right button takes one stem's clip away, from whichever stem is
    // selected, and selects that stem.
    select_stem(0);
    press(1, 1, Qt::RightButton);
    check(!song.hasClip(1, 1) && song.hasClip(0, 1) && song.hasClip(2, 1) &&
          song.selectedTrack() == 1);
    // Ctrl and an empty bar: that stem alone plays its part of the open
    // pattern there.
    press(1, 2, Qt::LeftButton, Qt::ControlModifier);
    check(song.hasClip(1, 2) && !song.hasClip(0, 2) && !song.hasClip(2, 2));
    // Pressing a filled bar opens its pattern for that stem.
    song.selectSection(0);
    select_stem(0);
    press(1, 2, Qt::LeftButton);
    check(song.selectedTrack() == 1 && song.currentSection() == chorus &&
          song.currentPattern() == song.partOf(chorus, 1));
    reached("stems: pressing the lanes places, removes and opens the right stems");

    // --- The right button on another stem's name offers that stem's menu. ---
    select_stem(1);
    {
        auto* header = ctx.named("trackHeader0");
        check(VerifyContext::usable(header, 40, 16));
        if (header != nullptr)
            ctx.click_at(header, {header->width() / 2.0, header->height() / 2.0},
                         Qt::RightButton);
        lay_out();
        auto* menu = ctx.window->findChild<QObject*>("trackMenu");
        const bool offered = menu != nullptr && menu->property("opened").toBool() &&
                             menu->property("target").toInt() == 0;
        check(offered && song.selectedTrack() == 0);
        if (!offered)
            std::cerr << "REGRESSION: the right button on a stem that was not selected did "
                         "not open its track menu\n";
        if (menu != nullptr) QMetaObject::invokeMethod(menu, "close");
        lay_out();
    }
    reached("stems: the right button on another stem's name offers that stem's menu");

    // --- A double click on another stem's name asks to rename that stem. ----
    select_stem(1);
    {
        auto* header = ctx.named("trackHeader0");
        check(VerifyContext::usable(header, 40, 16));
        if (header != nullptr) {
            // As the platform delivers a double click: the second press is
            // followed by the double click itself.
            const QPointF middle(header->width() / 2.0, header->height() / 2.0);
            ctx.mouse_at(header, middle, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
            ctx.mouse_at(header, middle, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
            ctx.mouse_at(header, middle, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
            ctx.mouse_at(header, middle, QEvent::MouseButtonDblClick, Qt::LeftButton,
                         Qt::LeftButton);
            ctx.mouse_at(header, middle, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        }
        lay_out();
        auto* popup = ctx.window->findChild<QObject*>("renamePopup");
        const bool asked = popup != nullptr && popup->property("opened").toBool() &&
                           popup->property("kind").toString() == "TRACK" &&
                           popup->property("target").toInt() == 0;
        check(asked && song.selectedTrack() == 0);
        if (!asked)
            std::cerr << "REGRESSION: a double click on a stem that was not selected did not "
                         "ask to rename it\n";
        if (popup != nullptr) QMetaObject::invokeMethod(popup, "close");
        lay_out();
    }
    reached("stems: a double click on another stem's name asks to rename it");

    // --- Saved and opened again, every stem keeps its own part. -------------
    const QString project = ctx.parser.value("project");
    if (!project.isEmpty()) {
        check(controller.saveProject(project));
        check(controller.loadProject(project));
        lay_out();
        check(song.sections().size() == 2 && song.trackCount() == 3 &&
              note_count(part_of(song.partOf(0, 0))) == 4 &&
              note_count(part_of(song.partOf(0, 1))) == 2 &&
              note_count(part_of(song.partOf(0, 2))) == 1);
        reached("stems: saved and opened, every stem keeps its own part");
    }

    // Left showing VERSE, STEM B's part, with the rack and the lanes.
    song.selectSection(0);
    select_stem(1);
    if (auto* scroll = ctx.named("editorScroll")) scroll->setProperty("contentY", 0);
    lay_out();
    check(ctx.save_screenshot());
    reached("screenshot");

    std::ostringstream details;
    details << " | A=" << note_count(part_of(song.partOf(0, 0)))
            << " B=" << note_count(part_of(song.partOf(0, 1)))
            << " stems=" << song.song().tracks.size() << " sections=" << song.sections().size();
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("stems", run_stems);

}  // namespace
}  // namespace blokkily::verify
