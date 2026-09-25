// features/tempo_meter.feature, run on its own by the bdd_tempo_meter gate
// (`--scenario tempo-meter`). Everything is driven through the rendered
// window: the ruler's meter menu makes bar 2 a bar of 7/8, the ruler draws it
// 7/8 as wide, a pattern made there has fourteen steps in the grid, the
// tracker and the roll, a Ctrl-click on the grid seeks into the 7/8 pass, the
// LEN spinner changes a pattern's length, and the tempo lane adds a point,
// drags it and ramps into it. What the song sounds like is read from the
// production render callback on the real CLAP fixture (which sounds DC while
// a note is held, so every note is an edge), against sample positions worked
// out here in closed form rather than by asking the clock under test.

#include "verify/harness.hpp"

#include "blokkily/model/timebase.hpp"

#include <QCoreApplication>
#include <QMouseEvent>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace blokkily::verify {
namespace {

constexpr double rate = 48000.0;

std::string list(const std::vector<long long>& values) {
    std::ostringstream out;
    out << '{';
    for (std::size_t index = 0; index < values.size(); ++index)
        out << (index ? ", " : "") << values[index];
    out << '}';
    return out.str();
}

// Every item called `name` in the visual tree; a closed menu keeps its items.
void collect(QQuickItem* from, const QString& name, std::vector<QQuickItem*>& found) {
    if (from == nullptr) return;
    if (from->objectName() == name) found.push_back(from);
    for (auto* child : from->childItems()) collect(child, name, found);
}

// The song the gate starts from: one 4/4 pattern of four notes on one track,
// placed once on bar 1, so every note heard later is one the gate put there.
blokkily::Song clean_song() {
    blokkily::Song song;
    blokkily::Pattern verse(1920, 480);
    for (const int step : {0, 4, 8, 12}) {
        blokkily::Trigger trigger;
        trigger.start = step * 120;
        trigger.duration = 96;
        trigger.musical_data = blokkily::Note{60, 0.9F, 0.0F};
        (void)verse.add(trigger);
    }
    song.patterns = {{"VERSE", std::move(verse)}};
    song.tracks = {blokkily::Track{}};
    song.tracks[0].name = "LEAD";
    // Hard left, so the left channel is the track at unity.
    song.tracks[0].mix.pan = -1.0;
    song.clips = {{0, 0, 0, 1}};
    return song;
}

void run_tempo_meter(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    auto& transport = ctx.transport;
    auto& controller = ctx.controller;
    const auto& parser = ctx.parser;
    const auto check = [&ctx](bool ok) { ctx.check(ok); };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto settle = [](int milliseconds) { VerifyContext::settle(milliseconds); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    const auto text_of = [&ctx](const char* name) {
        auto* item = ctx.named(QString::fromLatin1(name));
        return item == nullptr ? QString() : item->property("text").toString();
    };
    const auto press = [&ctx](const char* name, Qt::MouseButton button = Qt::LeftButton,
                              Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        auto* item = ctx.named(QString::fromLatin1(name));
        if (item == nullptr) return false;
        ctx.click_at(item, {item->width() / 2, item->height() / 2}, button, modifiers);
        return true;
    };
    // The one item of that name that is showing: a menu opened on screen.
    const auto showing = [window](const QString& name) -> QQuickItem* {
        std::vector<QQuickItem*> found;
        collect(window->contentItem(), name, found);
        for (auto* item : found)
            if (item->isVisible() && item->width() > 0 && item->height() > 0) return item;
        return nullptr;
    };
    // Picks num/den from the meter menu of ruler bar `bar`, as a producer
    // does: a right-click on the bar, then a click on the entry.
    const auto pick_meter = [&](int bar, int numerator, int denominator) {
        lay_out();
        const QString ruler = QString("rulerBar%1").arg(bar);
        auto* cell = ctx.named(ruler);
        if (cell == nullptr) return false;
        ctx.click_at(cell, {cell->width() / 2, cell->height() / 2}, Qt::RightButton);
        settle(60);
        lay_out();
        auto* entry = showing(QString("meterChoice%1_%2").arg(numerator).arg(denominator));
        if (entry == nullptr) return false;
        ctx.click_at(entry, {entry->width() / 2, entry->height() / 2}, Qt::LeftButton);
        settle(60);
        lay_out();
        return true;
    };
    auto* const output = ctx.output;
    // The left channel of the next `frames` frames of the production callback,
    // and the song sample the first of them was.
    const auto listen = [&](std::size_t frames, std::uint64_t* first) {
        controller.flushRecompile();
        if (first != nullptr)
            *first = controller.engine() != nullptr ? controller.engine()->sample_position() : 0;
        std::vector<float> left;
        std::vector<float> stereo(2048);
        while (left.size() < frames) {
            std::fill(stereo.begin(), stereo.end(), 0.0F);
            if (output == nullptr || !output->pump(stereo)) break;
            left.insert(left.end(), stereo.begin(), stereo.begin() + 1024);
        }
        return left;
    };
    // Song samples where a note starts: the signal rising out of silence.
    const auto onsets = [](const std::vector<float>& left, std::uint64_t first,
                           std::uint64_t before) {
        std::vector<long long> found;
        bool silent = true;
        for (std::size_t index = 0; index < left.size(); ++index) {
            const bool now_silent = std::abs(left[index]) <= 1e-4F;
            const auto at = first + index;
            if (silent && !now_silent && at < before) found.push_back(static_cast<long long>(at));
            silent = now_silent;
        }
        return found;
    };
    const auto engine_at = [&controller](double tick) {
        auto* engine = controller.engine();
        return engine != nullptr &&
               engine->sample_position() == blokkily::sample_for_tick(engine->published_clock(), tick);
    };

    // ------------------------------------------------------------------ setup
    song.replace(clean_song());
    check(controller.verifyClap(parser.value("clap-fixture")));
    check(controller.engine() != nullptr &&
          song.song().tracks.at(0).instrument.format == "CLAP");
    song.selectTrack(0);
    song.selectPattern(0);
    check(window->setProperty("view", "ALL"));
    controller.seekToBar(0);
    lay_out();
    check(text_of("meterReadout") == "4 / 4" && text_of("tempoReadout") == "120.00");
    reached("tempo-meter: a clean 4/4 song on the CLAP fixture");

    // ------------------------------------------- the ruler's meter menu
    const bool picked = pick_meter(1, 7, 8);
    check(picked);
    const auto& meter = song.song().meter;
    check(meter.changes.size() == 2 && meter.changes[1].bar == 1 &&
          meter.changes[1].numerator == 7 && meter.changes[1].denominator == 8);
    check(showing("meterChoice7_8") == nullptr);   // the menu closed
    check(text_of("meterReadout") == "4 / 4");     // the playhead is still in bar 1
    reached("tempo-meter: the ruler's meter menu makes bar 2 7/8");

    // ------------------------------------------------ proportional bars
    lay_out();
    auto* bar0 = ctx.named("rulerBar0");
    auto* bar1 = ctx.named("rulerBar1");
    auto* bar2 = ctx.named("rulerBar2");
    const double width0 = bar0 != nullptr ? bar0->width() : 0.0;
    const double width1 = bar1 != nullptr ? bar1->width() : 0.0;
    const double ratio = width0 > 0.0 ? width1 / width0 : 0.0;
    check(VerifyContext::usable(bar0, 12, 10) && VerifyContext::usable(bar1, 12, 10) &&
          VerifyContext::usable(bar2, 12, 10));
    check(std::abs(ratio - 7.0 / 8.0) < 0.02);
    if (std::abs(ratio - 7.0 / 8.0) >= 0.02)
        std::cerr << "ruler bars " << width0 << " and " << width1 << " wide, ratio " << ratio << '\n';
    // The clip cells under the ruler are the same width, lined up with it.
    auto* cell1 = ctx.named("clip0-1");
    check(cell1 != nullptr && bar1 != nullptr && std::abs(cell1->width() - width1) < 0.5 &&
          std::abs(cell1->mapToScene({0, 0}).x() - bar1->mapToScene({0, 0}).x()) < 0.5);
    check(text_of("rulerMeter1") == "7/8");
    auto* lane = ctx.named("tempoLane");
    check(VerifyContext::usable(lane, 400, 30));
    check(lane != nullptr && bar0 != nullptr &&
          std::abs(lane->mapToScene({0, 0}).x() - bar0->mapToScene({0, 0}).x()) < 0.5);
    reached("tempo-meter: a 7/8 bar is drawn 7/8 as wide, and the tempo lane is usable");

    // ------------------------------------------------ seeking to bar 3
    check(press("rulerBar2"));
    settle(20);
    check(transport.position() == "3.1.1" && transport.bar() == 2);
    check(engine_at(3600.0));
    check(controller.engine() != nullptr && controller.engine()->sample_position() == 180000);
    lay_out();
    check(text_of("meterReadout") == "7 / 8" && text_of("positionReadout") == "3.1.1");
    reached("tempo-meter: a click on bar 3 seeks to tick 3600, sample 180000");

    // ------------------------------------ a pattern made in the 7/8 bar
    check(press("addPatternButton"));
    lay_out();
    const int made = song.currentPattern();
    check(made == 1 && song.song().patterns.at(1).pattern.length() == 1680);
    check(pattern.stepCount() == 14 && text_of("patternLengthReadout") == "14 ST");
    check(ctx.named("step13") != nullptr && ctx.named("step14") == nullptr &&
          VerifyContext::usable(ctx.named("step13"), 12, 30));
    check(ctx.named("trackerRow13") != nullptr && ctx.named("trackerRow14") == nullptr);
    // The grid writes step 0; the roll, fourteen columns wide, writes step 13.
    if (auto* first = ctx.named("step0"))
        ctx.click_at(first, {first->width() / 2, first->height() / 2}, Qt::LeftButton);
    lay_out();
    if (auto* roll = ctx.named("rollInput")) {
        const int high = pattern.highKey();
        const double lane_height = roll->height() / std::max(1, high - pattern.lowKey() + 1);
        const double lane_width = roll->width() / 14.0;
        const int key = high - 3;   // near the top, which is on screen
        ctx.click_at(roll, {13.5 * lane_width, (high - key + 0.5) * lane_height}, Qt::LeftButton);
    } else {
        check(false);
    }
    lay_out();
    // Regression: the roll's hint made its header wider than its panel, so
    // the roll ran past the panel's edge and its last columns (step 13 of 14,
    // steps 14 and 15 of 16) could be neither seen nor clicked.
    {
        auto* panel = ctx.named("pianoRollView");
        auto* input = ctx.named("rollInput");
        const bool inside = panel != nullptr && input != nullptr &&
            input->mapToScene({input->width(), 0}).x() <=
                panel->mapToScene({panel->width(), 0}).x() + 0.5;
        check(inside);
        if (!inside) std::cerr << "REGRESSION: the piano roll runs past its panel\n";
    }
    check(pattern.hasStep(0) && pattern.hasStep(13));
    check(ctx.rendered_step(13) && ctx.roll_draws(13) && ctx.tracker_note(13) != "---");
    if (auto* note = ctx.named("rollNote13"); note != nullptr) {
        auto* roll = ctx.named("rollInput");
        // The note sits in the last of fourteen columns, not the fourteenth of sixteen.
        check(roll != nullptr &&
              std::abs(roll->mapFromScene(note->mapToScene({0, 0})).x() -
                       13.0 * roll->width() / 14.0) < 2.0);
    } else {
        check(false);
    }
    reached("tempo-meter: a pattern made in a 7/8 bar has 14 steps in the grid, tracker and roll");

    // ------------------------ placed in both 7/8 bars, heard from the pump
    check(press("clip0-1"));
    lay_out();
    check(press("clip0-2"));
    lay_out();
    check(song.song().clips.size() == 3 && song.hasClip(0, 1) && song.hasClip(0, 2));
    check(press("rulerBar1"));
    controller.togglePlayback();
    std::uint64_t first = 0;
    auto heard = listen(170 * 1024, &first);
    controller.togglePlayback();
    const auto found = onsets(heard, first, 264000);
    const std::vector<long long> expected{96000, 174000, 180000, 258000};
    check(first == 96000 && found == expected);
    if (found != expected)
        std::cerr << "7/8 onsets from " << first << ": " << list(found) << ", wanted "
                  << list(expected) << '\n';
    reached("tempo-meter: the downbeat of bar 3 sounds at sample_at(3600), each step of a 7/8 bar in place");

    // ------------------------------- Ctrl-click seeks within the 7/8 pass
    check(press("rulerBar2"));
    lay_out();
    check(press("step5", Qt::LeftButton, Qt::ControlModifier));
    // Step 5 of the pass starting at tick 3600 is tick 4200, not step 37 of
    // sixteen-step bars (tick 4440).
    check(std::abs(transport.tick() - 4200.0) < 1e-6 && transport.step() == 5 &&
          pattern.selectedStep() == 5);
    check(engine_at(4200.0));
    check(controller.engine() != nullptr && controller.engine()->sample_position() == 210000);
    reached("tempo-meter: a Ctrl-click on the grid seeks to that step of the 7/8 pass");

    // ------------------------------------------------- the LEN spinner
    lay_out();
    check(VerifyContext::usable(ctx.named("patternLength"), 70, 18) &&
          VerifyContext::usable(ctx.named("patternLengthDown"), 14, 14) &&
          VerifyContext::usable(ctx.named("patternLengthUp"), 14, 14));
    check(press("patternLengthDown"));
    lay_out();
    check(press("patternLengthDown"));
    lay_out();
    check(pattern.stepCount() == 12 && !pattern.hasStep(13) && pattern.hasStep(0));
    check(ctx.named("step11") != nullptr && ctx.named("step12") == nullptr &&
          ctx.named("trackerRow12") == nullptr && text_of("patternLengthReadout") == "12 ST");
    check(song.song().patterns.at(1).pattern.length() == 1440);
    check(press("patternLengthUp"));
    lay_out();
    check(pattern.stepCount() == 13 && !pattern.hasStep(13));
    check(song.undo() && song.undo() && song.undo());
    lay_out();
    check(pattern.stepCount() == 14 && pattern.hasStep(13) && ctx.named("step13") != nullptr);
    reached("tempo-meter: the LEN spinner sets the pattern's length and undo brings its steps back");

    // ------------------------------------------------- the tempo lane
    lay_out();
    lane = ctx.named("tempoLane");
    bar2 = ctx.named("rulerBar2");
    if (lane != nullptr && bar2 != nullptr) {
        // A double-click just inside bar 3 adds a point on its downbeat.
        const double x = lane->mapFromScene(bar2->mapToScene({0, 0})).x() + 2.0;
        const QPointF spot(x, lane->height() / 2);
        ctx.mouse_at(lane, spot, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        ctx.mouse_at(lane, spot, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        // The sequence a platform delivers: the second press, then the
        // double-click, then the release.
        ctx.mouse_at(lane, spot, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        ctx.mouse_at(lane, spot, QEvent::MouseButtonDblClick, Qt::LeftButton, Qt::LeftButton);
        ctx.mouse_at(lane, spot, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
    } else {
        check(false);
    }
    lay_out();
    const auto& tempo = song.song().tempo;
    check(tempo.points.size() == 2 && tempo.points[1].at == 3600 && tempo.points[1].bpm == 120.0);
    reached("tempo-meter: a double-click on the tempo lane adds a point on the beat");

    // Dragging its handle up 60 px is +30 BPM, one step of history.
    auto* handle = ctx.named("tempoPoint1");
    check(VerifyContext::usable(handle, 10, 10));
    const bool could_undo = song.canUndo();
    if (handle != nullptr && lane != nullptr) {
        // The pointer's path is fixed in the lane, which does not move; the
        // handle follows it up.
        const QPointF grab =
            lane->mapFromScene(handle->mapToScene({handle->width() / 2, handle->height() / 2}));
        ctx.mouse_at(lane, grab, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        for (int moved = 10; moved <= 60; moved += 10)
            ctx.mouse_at(lane, {grab.x(), grab.y() - moved}, QEvent::MouseMove, Qt::NoButton,
                         Qt::LeftButton);
        ctx.mouse_at(lane, {grab.x(), grab.y() - 60}, QEvent::MouseButtonRelease, Qt::LeftButton,
                     Qt::NoButton);
    }
    lay_out();
    check(tempo.points.size() == 2 && std::abs(tempo.points[1].bpm - 150.0) < 1e-9);
    check(could_undo && song.undo());
    check(tempo.points.size() == 2 && tempo.points[1].bpm == 120.0);
    check(song.redo() && tempo.points.size() == 2 &&
          std::abs(tempo.points[1].bpm - 150.0) < 1e-9);
    lay_out();
    check(text_of("tempoLabel1") == "150");
    reached("tempo-meter: dragging a tempo point's handle sets its tempo in one step of history");

    // The diamond after the first point turns the step into a ramp.
    check(press("tempoRamp0"));
    lay_out();
    check(tempo.points.size() == 2 && tempo.points[0].ramp && !tempo.points[1].ramp);
    check(press("rulerBar2"));
    lay_out();
    check(std::abs(transport.bpm() - 150.0) < 1e-9 && text_of("tempoReadout") == "150.00");
    check(press("rulerBar1"));
    lay_out();
    // Halfway up a ramp from 120 at tick 0 to 150 at tick 3600.
    check(std::abs(transport.bpm() - (120.0 + 30.0 * 1920.0 / 3600.0)) < 1e-9);
    // Where each tick sounds, integrated by hand: 480 ticks a beat, tempo
    // 120 + 30 t / 3600 up to tick 3600, then 150.
    const auto seconds = [](double tick) {
        if (tick <= 3600.0) return 15.0 * std::log(1.0 + tick / 14400.0);
        return 15.0 * std::log(1.25) + (tick - 3600.0) / 1200.0;
    };
    const auto sample_of = [&seconds](double tick) {
        return static_cast<long long>(std::floor(seconds(tick) * rate));
    };
    controller.togglePlayback();
    heard = listen(140 * 1024, &first);
    controller.togglePlayback();
    const auto song_end = static_cast<std::uint64_t>(sample_of(5280.0));
    const auto ramped = onsets(heard, first, song_end);
    const std::vector<long long> wanted{sample_of(1920.0), sample_of(3480.0), sample_of(3600.0),
                                        sample_of(5160.0)};
    bool on_time = std::llabs(static_cast<long long>(first) - wanted[0]) <= 1 &&
                   ramped.size() == wanted.size();
    for (std::size_t index = 0; on_time && index < wanted.size(); ++index)
        on_time = std::llabs(ramped[index] - wanted[index]) <= 1;
    check(on_time);
    if (!on_time)
        std::cerr << "ramped onsets from " << first << ": " << list(ramped) << ", wanted "
                  << list(wanted) << '\n';
    reached("tempo-meter: the ramp and the new tempo are heard where the tempo map puts them");

    // --------------------------------- a take in the 7/8 bar lands on its step
    controller.refreshMidiPorts();
    check(controller.selectMidiPort(0));
    auto& midi = controller.midiInput();
    const auto send = [&midi](std::uint8_t status, int key, int velocity) {
        const std::array<std::uint8_t, 3> message{
            status, static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(velocity)};
        return midi.inject(message);
    };
    check(press("rulerBar2"));
    lay_out();
    check(press("step6", Qt::LeftButton, Qt::ControlModifier));
    check(std::abs(transport.tick() - 4320.0) < 1e-6 && !pattern.hasStep(6));
    controller.toggleRecord();
    check(controller.recordArmed());
    controller.togglePlayback();
    (void)ctx.pump();
    check(send(0x90, 67, 100));
    for (int block = 0; block < 3; ++block) (void)ctx.pump();
    check(send(0x80, 67, 0));
    (void)ctx.pump();
    settle(120);
    controller.togglePlayback();
    (void)ctx.pump();
    controller.toggleRecord();
    check(!controller.recordArmed());
    settle(40);
    lay_out();
    check(song.currentPattern() == made && pattern.hasStep(6) && pattern.stepKey(6) == 67);
    check(ctx.rendered_step(6) && ctx.roll_draws(6));
    // Nothing was written into the 4/4 pattern.
    check(song.song().patterns.at(0).pattern.events().size() == 4);
    reached("tempo-meter: a take played in the 7/8 bar lands on the step it was played at");

    // ------------------------------------------- save, load and undo
    const auto saved_tempo = song.song().tempo;
    const auto saved_meter = song.song().meter;
    const QString project = parser.value("project");
    check(!project.isEmpty() && controller.saveProjectFile(project));
    check(pick_meter(1, 4, 4));
    check(song.barLayout().at(1).toMap().value("numerator").toInt() == 4 &&
          song.barLayout().at(1).toMap().value("denominator").toInt() == 4);
    check(controller.loadProjectFile(project));
    settle(40);
    lay_out();
    check(song.song().tempo == saved_tempo && song.song().meter == saved_meter);
    check(song.song().patterns.size() == 2 && song.song().patterns.at(1).pattern.length() == 1680);
    song.selectPattern(1);
    lay_out();
    check(pattern.stepCount() == 14 && pattern.hasStep(0) && pattern.hasStep(6) &&
          pattern.hasStep(13));
    check(controller.engine() != nullptr);
    // A meter chosen after the load is one step of history.
    check(pick_meter(3, 5, 4));
    check(song.barLayout().at(3).toMap().value("numerator").toInt() == 5);
    check(song.undo() && song.song().meter == saved_meter);
    check(song.redo() && song.barLayout().at(3).toMap().value("numerator").toInt() == 5);
    check(song.undo() && song.song().meter == saved_meter);
    lay_out();
    // Bar 4 is 7/8 like bar 3 again, so it no longer names a meter.
    auto* meter3 = ctx.named("rulerMeter3");
    check(meter3 != nullptr && !meter3->isVisible());
    reached("tempo-meter: save, load and undo keep the tempo and meter maps");

    // --------------------------------------------------- screenshot
    song.selectTrack(0);
    song.selectPattern(1);
    check(press("rulerBar2"));
    pattern.selectStep(6);
    settle(40);
    lay_out();
    check(text_of("meterReadout") == "7 / 8" && text_of("tempoReadout") == "150.00" &&
          text_of("positionReadout") == "3.1.1");
    reached("tempo-meter: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    std::ostringstream details;
    details << " | ruler ratio=" << ratio << " | 7/8 onsets=" << list(found)
            << " | ramped onsets=" << list(ramped) << " (want " << list(wanted) << ")";
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("tempo-meter", run_tempo_meter);

}  // namespace
}  // namespace blokkily::verify
