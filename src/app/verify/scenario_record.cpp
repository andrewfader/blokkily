// features/record_everything.feature, part 1 (plan item 2.5), run on its own by
// the bdd_record_everything gate (`--scenario record`). Two tracks, each
// playing the real CLAP fixture, one panned hard left and one hard right, are
// armed from the R on their mixer strips. With the transport running and
// recording, a key of the on-screen piano is clicked and a tracker note key is
// typed: both are heard on both tracks - read off the left and right halves of
// what the production callback rendered - and both land in both tracks'
// patterns, on the step nearest where they were played with the micro-timing
// they were played at, while the tracker cursor stays where it was. Stopped,
// the same tracker key writes the selected step and advances, as it always
// has. Arm and channel are saved and loaded, and undo and redo leave them be.

#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QKeyEvent>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <variant>
#include <vector>

namespace blokkily::verify {
namespace {

// The ticks a step spans, and where write_played puts a note heard at `tick`.
struct Placement {
    int step = 0;
    Tick micro = 0;
};
Placement place(Tick tick, Tick length) {
    const Tick per_step = PatternModel::ticks_per_step;
    const Tick at = ((tick % length) + length) % length;
    Tick step = (at + per_step / 2) / per_step;
    Tick micro = at - step * per_step;
    if (step >= length / per_step) {
        step = 0;
        micro = at - length;
    }
    return {static_cast<int>(step), micro};
}

// The trigger a pattern holds on a step, if any.
const Trigger* trigger_on(const Pattern& pattern, int step) {
    for (const auto& trigger : pattern.events())
        if (trigger.start / PatternModel::ticks_per_step == step) return &trigger;
    return nullptr;
}

bool holds_key(const Trigger* trigger, int key) {
    if (trigger == nullptr) return false;
    if (const auto* note = std::get_if<Note>(&trigger->musical_data)) return note->key == key;
    const auto& chord = std::get<Chord>(trigger->musical_data);
    for (const auto interval : chord.intervals)
        if (chord.root + interval == key) return true;
    return false;
}

void run_record(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    auto& controller = ctx.controller;
    auto& keyboard = ctx.keyboard;
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
    const auto track_row = [&song](int track) { return song.tracks().at(track).toMap(); };

    // One block of the production callback, as the two halves of the bus.
    struct Sides {
        float left = -1.0F;
        float right = -1.0F;
    };
    std::vector<float> stereo(2048, 0.0F);
    const auto pump_sides = [&]() -> Sides {
        controller.flushRecompile();
        std::fill(stereo.begin(), stereo.end(), 0.0F);
        if (ctx.output == nullptr || !ctx.output->pump(stereo)) return {};
        Sides sides{0.0F, 0.0F};
        for (std::size_t frame = 0; frame < 1024; ++frame) {
            sides.left = std::max(sides.left, std::abs(stereo[frame]));
            sides.right = std::max(sides.right, std::abs(stereo[1024 + frame]));
        }
        return sides;
    };
    const auto key_event = [window](QEvent::Type type, Qt::Key key, const QString& text) {
        QKeyEvent event(type, key, Qt::NoModifier, text);
        QCoreApplication::sendEvent(window, &event);
    };

    // The CLAP fixture on both tracks, behind the production adapter, one on
    // each side of the bus.
    check(controller.verifyClap(parser.value("clap-fixture")));
    const auto clap = song.song().tracks.at(0).instrument;
    check(clap.format == "CLAP");
    song.setInstrument(1, clap);
    settle(20);
    check(controller.engine() != nullptr && controller.engine()->has_instrument(0) &&
          controller.engine()->has_instrument(1));
    controller.setTempo(120.0);
    song.setTrackPan(0, -1.0);
    song.setTrackPan(1, 1.0);
    // At unity, so each side of the bus carries its track's 0.25 exactly.
    song.setTrackGain(0, 0.0);
    song.setTrackGain(1, 0.0);
    // Bar 1: VERSE under track 0 and CHORUS under track 1, two patterns.
    song.selectPattern(1);
    song.placeClip(1, 0);
    song.selectTrack(0);
    check(window->setProperty("view", "ALL"));
    lay_out();
    const std::size_t verse = song.song().clips.at(0).pattern;
    check(verse == 0);
    reached("record: two panned CLAP tracks");

    // The R and the channel of each strip are there and big enough to use.
    check(VerifyContext::usable(ctx.named("arm0"), 18, 18) &&
          VerifyContext::usable(ctx.named("arm1"), 18, 18));
    check(VerifyContext::usable(ctx.named("inputChannel0"), 40, 18) &&
          VerifyContext::usable(ctx.named("inputChannel1"), 40, 18));
    check(VerifyContext::usable(ctx.named("mixerStrip0"), 150, 140) &&
          VerifyContext::usable(ctx.named("mixerStrip1"), 150, 140));
    check(!track_row(0).value("armed").toBool() && !track_row(1).value("armed").toBool());
    // Nothing armed: the keyboard plays the selected track.
    check(controller.midiInput().routes()[0] == track_bit(0));
    reached("record: the strips offer R and a channel");

    // Both armed from the rendered R.
    const bool could_undo = song.canUndo();
    // A strip is laid out again when its track changes, so each chip is found
    // afresh rather than held across a click.
    const auto click_named = [&ctx, &lay_out](const QString& name, Qt::MouseButton button) {
        auto* chip = ctx.named(name);
        if (chip != nullptr)
            ctx.click_at(chip, {chip->width() / 2, chip->height() / 2}, button);
        lay_out();
        return chip != nullptr;
    };
    const auto lit = [&ctx](const QString& name) {
        auto* chip = ctx.named(name);
        return chip != nullptr && chip->property("on").toBool();
    };
    check(click_named("arm0", Qt::LeftButton) && click_named("arm1", Qt::LeftButton));
    check(track_row(0).value("armed").toBool() && track_row(1).value("armed").toBool());
    check(lit("arm0") && lit("arm1"));
    check(text_of("inputState0") == "ARMED");
    for (const auto mask : controller.midiInput().routes())
        check(mask == (track_bit(0) | track_bit(1)));
    // Arming is not an edit of the music: history did not move.
    check(song.canUndo() == could_undo && !song.canRedo());
    // The channel steps forward on a click and back on a right click.
    check(click_named("inputChannel1", Qt::LeftButton) && text_of("inputChannel1") == "CH 1");
    check(click_named("inputChannel1", Qt::RightButton) && text_of("inputChannel1") == "ALL");
    reached("record: two tracks armed from their strips");

    // The tracker cursor sits on an empty step of the open pattern.
    constexpr int cursor = 9;
    check(!pattern.hasStep(cursor));
    pattern.selectStep(cursor);

    // The piano key to click: a plain twelve-tone middle C.
    int piano_cell = -1;
    const auto& cells = keyboard.layout();
    for (std::size_t index = 0; index < cells.size(); ++index)
        if (cells[index].pitch.key == 60 && std::abs(cells[index].pitch.cents) < 0.5 &&
            cells[index].chord.size() <= 1) {
            piano_cell = static_cast<int>(index);
            break;
        }
    check(piano_cell >= 0);
    const int piano_key = piano_cell < 0 ? 60
        : song.pitchForDegree(song.snapDegree(cells[static_cast<std::size_t>(piano_cell)].degree)).key;
    auto* cell = piano_cell < 0 ? nullptr
        : VerifyContext::child_named(ctx.item("keyboardSurface"),
                                     QString("keyboardCell%1").arg(piano_cell));
    auto* key_input = cell == nullptr ? nullptr : VerifyContext::child_named(cell, "keyInput");
    check(key_input != nullptr);
    if (key_input != nullptr) {
        // Scrolled into view, as a producer scrolls to it.
        if (auto* editors = ctx.item("editorScroll")) {
            const double overhang =
                key_input->mapToScene({0, key_input->height()}).y() - (window->height() - 8);
            if (overhang > 0)
                editors->setProperty("contentY", editors->property("contentY").toDouble() + overhang);
            lay_out();
        }
    }
    // The tracker writes Z in the entry octave.
    const int tracker_key = (3 + 1) * 12;

    // Record: play from step 3 of bar 1.
    controller.toggleRecord();
    check(controller.recordArmed());
    controller.seekToStep(2);
    controller.togglePlayback();
    check(controller.recordingLive());
    const auto quiet_before = pump_sides();
    (void)pump_sides();
    // Nothing of the arrangement sounds here, on either track.
    check(quiet_before.left == 0.0F && quiet_before.right == 0.0F);
    auto* engine = controller.engine();
    const auto clock = engine->published_clock();
    const auto length = static_cast<Tick>(song.song().patterns.at(verse).pattern.length());

    // A piano click, held for three blocks.
    const Tick piano_tick = tick_at_sample(clock, engine->sample_position());
    const QPointF middle = key_input == nullptr ? QPointF{}
        : QPointF(key_input->width() / 2, key_input->height() / 2);
    ctx.mouse_at(key_input, middle, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
    Sides piano{};
    for (int block = 0; block < 3; ++block) {
        const auto sides = pump_sides();
        piano.left = std::max(piano.left, sides.left);
        piano.right = std::max(piano.right, sides.right);
    }
    ctx.mouse_at(key_input, middle, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
    (void)pump_sides();
    const auto after_piano = pump_sides();
    check(std::abs(piano.left - 0.25F) < 1e-4F && std::abs(piano.right - 0.25F) < 1e-4F);
    check(after_piano.left == 0.0F && after_piano.right == 0.0F);
    reached("record: a piano click is heard on both armed tracks");

    // A tracker key, typed ten blocks later and held for three.
    for (int block = 0; block < 10; ++block) (void)pump_sides();
    const Tick tracker_tick = tick_at_sample(clock, engine->sample_position());
    key_event(QEvent::KeyPress, Qt::Key_Z, QStringLiteral("z"));
    Sides typed{};
    for (int block = 0; block < 3; ++block) {
        const auto sides = pump_sides();
        typed.left = std::max(typed.left, sides.left);
        typed.right = std::max(typed.right, sides.right);
    }
    key_event(QEvent::KeyRelease, Qt::Key_Z, QStringLiteral("z"));
    (void)pump_sides();
    const auto after_typed = pump_sides();
    check(std::abs(typed.left - 0.25F) < 1e-4F && std::abs(typed.right - 0.25F) < 1e-4F);
    check(after_typed.left == 0.0F && after_typed.right == 0.0F);
    // Typed into a take, the cursor did not move and wrote nothing.
    check(pattern.selectedStep() == cursor);
    reached("record: a tracker key is heard on both armed tracks");

    settle(60);
    controller.togglePlayback();
    (void)pump_sides();
    controller.toggleRecord();
    check(!controller.recordArmed() && !controller.recordingLive());
    settle(40);
    lay_out();

    // Both patterns hold both keys, on the nearest step, off the grid by
    // exactly as much as they were played off it.
    const auto piano_at = place(piano_tick, length);
    const auto tracker_at = place(tracker_tick, length);
    check(piano_at.micro != 0 && tracker_at.micro != 0 && piano_at.step != tracker_at.step);
    std::ostringstream landed;
    for (const std::size_t index : {verse, std::size_t{1}}) {
        const auto& held = song.song().patterns.at(index).pattern;
        const auto* piano_trigger = trigger_on(held, piano_at.step);
        const auto* tracker_trigger = trigger_on(held, tracker_at.step);
        const bool ok = holds_key(piano_trigger, piano_key) &&
                        piano_trigger->micro_offset == piano_at.micro &&
                        holds_key(tracker_trigger, tracker_key) &&
                        tracker_trigger->micro_offset == tracker_at.micro &&
                        trigger_on(held, cursor) == nullptr;
        check(ok);
        landed << " | pattern " << index << ": piano step " << piano_at.step << " micro "
               << (piano_trigger ? piano_trigger->micro_offset : -999) << " (want "
               << piano_at.micro << "), tracker step " << tracker_at.step << " micro "
               << (tracker_trigger ? tracker_trigger->micro_offset : -999) << " (want "
               << tracker_at.micro << ")";
    }
    if (!ctx.valid) std::cerr << "record take:" << landed.str() << '\n';
    // The open pattern (track 1's CHORUS) shows them in every editor.
    check(song.currentPattern() == 1);
    check(ctx.rendered_step(piano_at.step) && ctx.rendered_step(tracker_at.step));
    check(ctx.roll_draws(piano_at.step) && ctx.roll_draws(tracker_at.step));
    check(pattern.selectedStep() == cursor && !pattern.hasStep(cursor));
    reached("record: both patterns hold both keys with their micro-timing");

    // Stopped, the tracker key writes the selected step and moves on.
    key_event(QEvent::KeyPress, Qt::Key_Z, QStringLiteral("z"));
    key_event(QEvent::KeyRelease, Qt::Key_Z, QStringLiteral("z"));
    lay_out();
    check(pattern.hasStep(cursor) && pattern.stepKey(cursor) == tracker_key);
    check(pattern.selectedStep() == cursor + 1);
    reached("record: stopped, a tracker key writes the step and advances");

    // Undo and redo take back the step and the take, never the arm.
    const auto still_armed = [&] {
        return track_row(0).value("armed").toBool() && track_row(1).value("armed").toBool() &&
               controller.midiInput().routes()[0] == (track_bit(0) | track_bit(1));
    };
    check(song.undo());
    check(!pattern.hasStep(cursor) && still_armed());
    check(song.undo());
    check(trigger_on(song.song().patterns.at(1).pattern, piano_at.step) == nullptr &&
          still_armed());
    // Disarming between steps of history is not undone either.
    song.toggleArm(0);
    check(song.redo());
    check(!track_row(0).value("armed").toBool() && track_row(1).value("armed").toBool());
    song.toggleArm(0);
    check(song.redo());
    check(pattern.hasStep(cursor) && still_armed());
    check(holds_key(trigger_on(song.song().patterns.at(1).pattern, piano_at.step), piano_key));
    // Across a step that adds or removes a track, every other track keeps its
    // arm, and a track brought back keeps the arm it had.
    song.addTrack();
    song.setArmed(2, true);
    check(song.undo());
    check(song.trackCount() == 2 && still_armed());
    check(song.redo());
    check(song.trackCount() == 3 && track_row(0).value("armed").toBool() &&
          track_row(1).value("armed").toBool() && track_row(2).value("armed").toBool() &&
          controller.midiInput().routes()[0] == (track_bit(0) | track_bit(1) | track_bit(2)));
    song.setArmed(2, false);
    check(song.deleteTrack(0));
    check(song.trackCount() == 2 && track_row(0).value("armed").toBool() &&
          !track_row(1).value("armed").toBool());
    check(song.undo());
    check(song.trackCount() == 3 && still_armed() && !track_row(2).value("armed").toBool());
    check(song.undo());
    check(song.trackCount() == 2 && still_armed());
    settle(20);
    reached("record: undo and redo leave the arm alone");

    // Track 1 listens to channel 2 only. Saved and loaded, both keep their arm
    // and channel, and the keyboard is routed the same way.
    song.setInputChannel(1, 2);
    lay_out();
    check(text_of("inputChannel1") == "CH 2");
    const QString project = parser.value("project");
    if (!project.isEmpty()) {
        check(controller.saveProjectFile(project));
        song.setArmed(0, false);
        song.setArmed(1, false);
        song.setInputChannel(1, 0);
        check(controller.loadProjectFile(project));
        settle(40);
        lay_out();
    }
    check(track_row(0).value("armed").toBool() && track_row(1).value("armed").toBool());
    check(text_of("inputChannel1") == "CH 2" && text_of("inputChannel0") == "ALL");
    const auto routes = controller.midiInput().routes();
    check(routes[0] == track_bit(0) && routes[1] == (track_bit(0) | track_bit(1)));
    reached("record: arm and channel are saved and loaded");

    // Left with the take in the editors and both strips armed.
    song.selectPattern(1);
    pattern.selectStep(piano_at.step);
    if (auto* editors = ctx.item("editorScroll")) editors->setProperty("contentY", 0.0);
    settle(40);
    lay_out();
    reached("record: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    std::ostringstream details;
    details << " | piano L/R " << piano.left << '/' << piano.right << " | tracker L/R "
            << typed.left << '/' << typed.right << landed.str();
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("record", run_record);

}  // namespace
}  // namespace blokkily::verify
