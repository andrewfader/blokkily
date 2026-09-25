// features/automation.feature, part 2 of "record everything" (plan item 3.1),
// run on its own by the bdd_automation gate (`--scenario automation`). One
// track plays the real CLAP fixture, a key struck on every sixteenth, so the
// pump hears its level times the strip's gain. The track's mode chip is
// stepped from READ to TOUCH on the rendered strip; with the song playing and
// recording, a key is performed and the rendered GAIN fader is pressed,
// dragged down in steps and let go, each step heard in the pump. The take
// becomes a note in the pattern and a gain lane whose points the lane editor
// draws (automationPoint0_*); played again the lane sounds as the drag did,
// and one undo takes back the note, the lane and the fader together. The lane
// editor adds, drags and removes a point, and the lane and the mode survive a
// save and a load.

#include "verify/harness.hpp"

#include "blokkily/audio/mixer.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <variant>
#include <vector>

namespace blokkily::verify {
namespace {

constexpr float level = 0.25F;
constexpr float centre = 0.70710678F;

bool near(float value, float expected, float tolerance = 2e-3F) {
    return std::abs(value - expected) <= tolerance;
}

float expected_at(double gain_db) {
    return level * centre * static_cast<float>(db_to_linear(gain_db));
}

void run_automation(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& controller = ctx.controller;
    const auto& parser = ctx.parser;
    const auto check = [&ctx](bool ok) { ctx.check(ok); };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto settle = [](int milliseconds) { VerifyContext::settle(milliseconds); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    const auto text_of = [&ctx](const QString& name) {
        auto* item = ctx.named(name);
        return item == nullptr ? QString() : item->property("text").toString();
    };
    const auto click = [&ctx, &lay_out](QQuickItem* item, Qt::MouseButton button) {
        if (item != nullptr) ctx.click_at(item, {item->width() / 2, item->height() / 2}, button);
        lay_out();
        return item != nullptr;
    };
    // The loudest sample of a few blocks, once the key has been struck again.
    // A key lasts a sixteenth (6000 samples); eight blocks always reach the
    // next strike.
    const auto heard = [&ctx] {
        float loudest = 0.0F;
        for (int block = 0; block < 8; ++block) loudest = std::max(loudest, ctx.pump());
        return loudest;
    };
    const auto& lanes = [&song]() -> const std::vector<AutomationLane>& {
        return song.song().tracks.at(0).automation;
    };

    // The CLAP fixture on one track, a key on every sixteenth for two bars.
    check(controller.verifyClap(parser.value("clap-fixture")));
    const auto clap = song.song().tracks.at(0).instrument;
    check(clap.format == "CLAP");
    {
        Pattern pattern(1920, 480);
        for (Tick step = 0; step < 16; ++step) {
            Trigger trigger;
            trigger.start = step * 120;
            trigger.duration = 120;
            trigger.musical_data = Note{60, 1.0F, 0.0F};
            (void)pattern.add(trigger);
        }
        Song fresh;
        fresh.patterns = {{"KEYS", std::move(pattern)}};
        fresh.tracks = {Track{}};
        fresh.tracks[0].name = "CLAP LEAD";
        fresh.tracks[0].instrument = clap;
        fresh.clips = {{0, 0, 0, 2}};
        song.replace(std::move(fresh));
    }
    settle(20);
    check(window->setProperty("view", "ALL"));
    lay_out();
    check(controller.engine() != nullptr && controller.engine()->has_instrument(0));
    reached("automation: one CLAP track, a key on every sixteenth");

    // The mode chip is on the strip, big enough to use, and steps to TOUCH.
    auto* mode_chip = ctx.named("automationMode0");
    check(VerifyContext::usable(mode_chip, 48, 18));
    check(VerifyContext::usable(ctx.named("mixerStrip0"), 150, 170));
    check(text_of("automationMode0") == "READ");
    check(click(mode_chip, Qt::LeftButton));
    check(text_of("automationMode0") == "TOUCH" &&
          song.song().tracks.at(0).automation_mode == AutomationMode::touch);
    check(click(ctx.named("automationMode0"), Qt::RightButton) &&
          text_of("automationMode0") == "READ");
    check(click(ctx.named("automationMode0"), Qt::LeftButton) &&
          text_of("automationMode0") == "TOUCH");
    // The lane editor is there, and big enough to draw in, with no lane yet.
    auto* lane_view = ctx.named("automationLane");
    check(VerifyContext::usable(lane_view, 300, 40));
    check(VerifyContext::usable(ctx.named("automationHeader"), 60, 40));
    check(lanes().empty() && song.selectedLane() == -1);
    reached("automation: the mode chip steps READ to TOUCH");

    // The rendered GAIN fader of the strip.
    QQuickItem* fader = nullptr;
    if (auto* dial = ctx.named("gainDial0"))
        for (auto* child : dial->childItems())
            if (child->inherits("QQuickSlider")) fader = child;
    check(VerifyContext::usable(fader, 100, 12));

    // Play and record from the top: a key performed, then the fader pressed,
    // dragged down in three steps and let go, each step heard in the pump.
    auto* engine = controller.engine();
    const auto undo_depth_before = song.canUndo();
    const auto clock = engine->published_clock();
    controller.toggleRecord();
    controller.seekToTick(0);
    controller.togglePlayback();
    check(controller.recordingLive());
    for (int block = 0; block < 10; ++block) (void)ctx.pump();
    check(near(heard(), expected_at(0.0)));
    const Tick key_tick = tick_at_sample(clock, engine->sample_position());
    check(controller.performKey(72, false));
    for (int block = 0; block < 4; ++block) (void)ctx.pump();
    controller.releasePerformed();
    for (int block = 0; block < 4; ++block) (void)ctx.pump();

    const double y = fader == nullptr ? 0.0 : fader->height() / 2.0;
    const auto handle_x = [fader] {
        if (fader == nullptr) return 0.0;
        return fader->property("leftPadding").toDouble() +
               fader->property("visualPosition").toDouble() *
                   (fader->property("availableWidth").toDouble() - 12.0) +
               6.0;
    };
    double x = handle_x();
    const Tick touch_tick = tick_at_sample(clock, engine->sample_position());
    ctx.mouse_at(fader, {x, y}, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
    lay_out();
    check(controller.stripHeld(0, "gain"));
    for (int block = 0; block < 4; ++block) (void)ctx.pump();
    struct Step {
        Tick tick;
        double gain_db;
        float heard;
    };
    std::vector<Step> steps;
    const double travel = fader == nullptr ? 0.0 : fader->width() * 0.14;
    for (int step = 1; step <= 3; ++step) {
        x -= travel;
        ctx.mouse_at(fader, {x, y}, QEvent::MouseMove, Qt::NoButton, Qt::LeftButton);
        lay_out();
        const Tick at = tick_at_sample(clock, engine->sample_position());
        const double gain = song.song().tracks.at(0).mix.gain_db;
        const float sounded = heard();
        for (int block = 0; block < 32; ++block) (void)ctx.pump();
        steps.push_back({at, gain, sounded});
    }
    ctx.mouse_at(fader, {x, y}, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
    lay_out();
    const Tick release_tick = tick_at_sample(clock, engine->sample_position());
    check(!controller.stripHeld(0, "gain"));
    std::ostringstream drag;
    bool steps_heard = steps.size() == 3;
    for (std::size_t index = 0; index < steps.size(); ++index) {
        const auto& step = steps[index];
        drag << " | step " << index << " " << step.gain_db << " dB heard " << step.heard
             << " want " << expected_at(step.gain_db);
        steps_heard = steps_heard && near(step.heard, expected_at(step.gain_db)) &&
                      (index == 0 || step.gain_db < steps[index - 1].gain_db - 3.0);
    }
    check(steps_heard);
    // The release is played at the next block; the drain the meter timer
    // makes then writes the touch pass, and the lane plays from the next
    // recompile.
    (void)ctx.pump();
    settle(120);
    lay_out();
    check(lanes().size() == 1 && lanes()[0].target.kind == AutomationTarget::Kind::gain);
    const auto lane = lanes().empty() ? AutomationLane{} : lanes()[0];
    bool lane_holds = !lane.points.empty();
    for (const auto& step : steps) {
        const auto value = lane.value_at(step.tick + 20);
        lane_holds = lane_holds && value && std::abs(*value - step.gain_db) < 0.1;
    }
    check(lane_holds);
    check(lane.value_at(touch_tick / 2) && std::abs(*lane.value_at(touch_tick / 2)) < 1e-9);
    check(lane.value_at(release_tick + 60) && std::abs(*lane.value_at(release_tick + 60)) < 1e-9);
    // Let go in touch mode, the strip is back on its lane: 0 dB after the pass.
    for (int block = 0; block < 4; ++block) (void)ctx.pump();
    check(near(heard(), expected_at(0.0)));
    // The lane editor draws every point, each big enough to grab.
    const int drawn = static_cast<int>(lane.points.size());
    bool points_usable = drawn >= 4;
    for (int index = 0; index < drawn; ++index)
        points_usable = points_usable &&
                        VerifyContext::usable(ctx.named(QString("automationPoint0_%1").arg(index)),
                                              8, 8);
    check(points_usable);
    check(text_of("automationLaneName") == "GAIN");
    reached("automation: a fader dragged in touch mode becomes a lane");

    // Played again with nobody touching it, the lane sounds as the drag did.
    bool replayed = true;
    std::ostringstream replay;
    for (std::size_t index = 0; index < steps.size(); ++index) {
        // The first sixteenth (a strike of the key) the step held through,
        // and the block (21 ticks) from it.
        const Tick until = index + 1 < steps.size() ? steps[index + 1].tick : release_tick;
        const Tick strike = (steps[index].tick / 120 + 1) * 120;
        replayed = replayed && strike + 21 <= until;
        controller.seekToTick(static_cast<double>(strike));
        const float sounded = ctx.pump();
        replay << ' ' << sounded << " at " << strike;
        replayed = replayed && near(sounded, steps[index].heard);
    }
    check(replayed);
    reached("automation: the lane plays back what was heard");

    // Stopped, the take is one step of history: the note, the lane and the
    // fader go back together, and come back together.
    controller.togglePlayback();
    controller.toggleRecord();
    settle(40);
    // The performed key, alone on its step or merged into the step's key.
    const auto holds_note = [&song] {
        for (const auto& trigger : song.song().patterns.at(0).pattern.events()) {
            if (const auto* note = std::get_if<Note>(&trigger.musical_data)) {
                if (note->key == 72) return true;
                continue;
            }
            const auto& chord = std::get<Chord>(trigger.musical_data);
            for (const auto interval : chord.intervals)
                if (chord.root + interval == 72) return true;
        }
        return false;
    };
    const bool taken = holds_note() && lanes().size() == 1 &&
                       song.song().tracks[0].mix.gain_db < -10.0;
    if (!taken)
        std::cerr << "take: note " << holds_note() << " lanes " << lanes().size() << " gain "
                  << song.song().tracks[0].mix.gain_db << '\n';
    check(taken);
    check(song.undo());
    settle(20);
    lay_out();
    check(!holds_note() && lanes().empty() &&
          std::abs(song.song().tracks[0].mix.gain_db) < 1e-9 &&
          song.song().tracks[0].automation_mode == AutomationMode::touch);
    check(song.canUndo() == true && undo_depth_before);
    check(song.redo());
    settle(20);
    lay_out();
    check(holds_note() && lanes().size() == 1 && lanes()[0] == lane);
    reached("automation: one undo takes back the take");

    // The lane editor: a double-click adds a point, a drag moves it, the
    // right button removes it.
    lane_view = ctx.named("automationLane");
    const auto points_now = [&] { return lanes().empty() ? 0 : lanes()[0].points.size(); };
    const auto before_edit = points_now();
    if (lane_view != nullptr) {
        // Three quarters through the second bar, halfway up.
        const QPointF spot(lane_view->width() * 0.87, lane_view->height() / 2);
        ctx.mouse_at(lane_view, spot, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        ctx.mouse_at(lane_view, spot, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        // The sequence a platform delivers: the second press, then the
        // double-click, then the release.
        ctx.mouse_at(lane_view, spot, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        ctx.mouse_at(lane_view, spot, QEvent::MouseButtonDblClick, Qt::LeftButton,
                     Qt::LeftButton);
        ctx.mouse_at(lane_view, spot, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        lay_out();
    }
    check(points_now() == before_edit + 1);
    const auto last_index = static_cast<int>(points_now()) - 1;
    const auto added = lanes().empty() ? AutomationPoint{} : lanes()[0].points.back();
    check(added.at > 3 * 1920 / 2 && added.value > -40.0 && added.value < -10.0);
    auto* handle = ctx.named(QString("automationPoint0_%1").arg(last_index));
    check(VerifyContext::usable(handle, 8, 8));
    if (handle != nullptr) {
        const QPointF middle(handle->width() / 2, handle->height() / 2);
        ctx.mouse_at(handle, middle, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        for (int step = 1; step <= 4; ++step)
            ctx.mouse_at(handle, middle - QPointF(0, 3.0 * step), QEvent::MouseMove,
                         Qt::NoButton, Qt::LeftButton);
        ctx.mouse_at(handle, middle - QPointF(0, 12.0), QEvent::MouseButtonRelease,
                     Qt::LeftButton, Qt::NoButton);
        lay_out();
    }
    const auto moved = lanes().empty() ? AutomationPoint{} : lanes()[0].points.back();
    check(points_now() == before_edit + 1 && moved.value > added.value + 3.0);
    if (!ctx.valid)
        std::cerr << "editor: before " << before_edit << " now " << points_now() << " added "
              << added.at << '/' << added.value << " moved " << moved.at << '/' << moved.value
              << " selected lane " << song.selectedLane() << '\n';
    // Heard: the engine was recompiled, not rebuilt, with the moved point.
    handle = ctx.named(QString("automationPoint0_%1").arg(last_index));
    check(click(handle, Qt::RightButton));
    check(points_now() == before_edit);
    reached("automation: points drawn, moved and removed in the lane editor");

    // Saved and loaded, the lane and the mode come back.
    const auto kept = lanes();
    const QString project = parser.value("project");
    if (!project.isEmpty()) {
        check(controller.saveProjectFile(project));
        song.setAutomationMode(0, "OFF");
        check(controller.loadProjectFile(project));
        settle(40);
        lay_out();
    }
    check(lanes() == kept && text_of("automationMode0") == "TOUCH");
    reached("automation: lanes and mode are saved and loaded");

    // Left with the lane in the editor and the playhead in the pass.
    controller.seekToTick(static_cast<double>(steps.empty() ? 0 : steps[1].tick));
    if (auto* editors = ctx.item("editorScroll")) editors->setProperty("contentY", 0.0);
    settle(40);
    lay_out();
    reached("automation: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    std::ostringstream details;
    details << " | key at tick " << key_tick << " | touch " << touch_tick << " release "
            << release_tick << drag.str() << " | replay" << replay.str() << " | lane points "
            << lane.points.size();
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("automation", run_automation);

}  // namespace
}  // namespace blokkily::verify
