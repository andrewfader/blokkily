// features/midi_input_and_recording.feature, run on its own by the
// bdd_midi_recording gate (`--scenario midi`). A MIDI keyboard is played into
// the real application: the port is chosen from the rendered panel, keys are
// heard through the production render callback, and an armed song writes what
// was played into the pattern under the playhead, where every editor shows it
// and the arrangement plays it back.

#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QMetaObject>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <vector>

namespace blokkily::verify {
namespace {

void run_midi(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    auto& transport = ctx.transport;
    auto& controller = ctx.controller;
    auto* const verification_output = ctx.output;
    const auto& parser = ctx.parser;
    const auto named = [&ctx](const QString& name) { return ctx.named(name); };
    const auto click_at = [&ctx](QQuickItem* item, QPointF local, Qt::MouseButton button) {
        ctx.click_at(item, local, button);
    };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto check = [&ctx](bool ok) { ctx.check(ok); };
    const auto settle = [](int milliseconds) { VerifyContext::settle(milliseconds); };
    const auto pump = [&ctx] { return ctx.pump(); };
    const auto rendered_step = [&ctx](int step) { return ctx.rendered_step(step); };
    const auto roll_draws = [&ctx](int step) { return ctx.roll_draws(step); };
    const auto tracker_note = [&ctx](int row) { return ctx.tracker_note(row); };

    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    const auto text_of = [&named](const char* name) {
        auto* label = named(QString::fromLatin1(name));
        return label == nullptr ? QString() : label->property("text").toString();
    };
    auto& midi = controller.midiInput();
    const auto send = [&midi](std::uint8_t status, int key, int velocity) {
        const std::array<std::uint8_t, 3> message{
            status, static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(velocity)};
        return midi.inject(message);
    };

    // The CLAP fixture on track 0, the VST3 fixture on track 1: real
    // instruments behind the production adapters.
    check(controller.verifyClap(parser.value("clap-fixture")));
    check(controller.verifyVst3(parser.value("vst3-fixture")));
    check(controller.engine() != nullptr
       && song.song().tracks.at(0).instrument.format == "CLAP"
       && song.song().tracks.at(1).instrument.format == "VST3");
    controller.setTempo(120.0);
    song.selectTrack(0);
    check(window->setProperty("view", "ALL"));
    lay_out();
    reached("midi: instruments");

    // The panel is there, usable, and lists the port.
    {
        auto* panel = named("midiPanel");
        check(VerifyContext::usable(panel, 120, 24));
        check(text_of("midiPortName") == "No MIDI input");
        check(controller.midiPorts() == QStringList{"Deterministic input"});
        if (panel != nullptr)
            click_at(panel, {panel->width() / 2, panel->height() / 2}, Qt::LeftButton);
        lay_out();
        auto* menu = window->findChild<QObject*>("midiMenu");
        check(menu != nullptr && menu->property("opened").toBool()
           && menu->property("count").toInt() == 2);
        // Choose the port by clicking its row of the open menu.
        QQuickItem* row = nullptr;
        if (menu != nullptr)
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, row),
                                      Q_ARG(int, 1));
        check(row != nullptr
           && row->property("text").toString() == "Deterministic input");
        if (row != nullptr)
            click_at(row, {row->width() / 2, row->height() / 2}, Qt::LeftButton);
        lay_out();
        check(controller.midiPort() == "Deterministic input"
           && text_of("midiPortName") == "Deterministic input"
           && verification_output != nullptr
           && verification_output->is_running());
    }
    reached("midi: a port is chosen from the rendered panel");

    // A key is heard on a stopped song, and nothing is written.
    const int events_before = pattern.rowCount();
    {
        check(!transport.playing() && pump() == 0.0F);
        check(send(0x90, 60, 100));
        const float held = pump();
        check(held > 0.05F && controller.engine()->track_peak(0) > 0.05F
           && controller.engine()->track_peak(1) == 0.0F);
        check(pump() > 0.05F);   // it keeps sounding while held
        check(send(0x80, 60, 0));
        check(pump() == 0.0F);
        // The panel says what arrived, once the interface has polled.
        settle(80);
        lay_out();
        check(controller.midiNotes() == 1
           && text_of("midiActivity").endsWith(" · 100")
           && text_of("midiActivity").startsWith(song.degreeName(60)));
        check(pattern.rowCount() == events_before);
    }
    reached("midi: a key is heard on a stopped song");

    // The selected track is the one that sounds.
    {
        song.selectTrack(1);
        check(send(0x90, 62, 100));
        (void)pump();
        const bool second = controller.engine()->track_peak(1) > 0.0F
                            && controller.engine()->track_peak(0) == 0.0F;
        // Switching back mid-note still releases it on track 1.
        song.selectTrack(0);
        check(send(0x80, 62, 0));
        for (int block = 0; block < 64 && pump() != 0.0F; ++block) {}
        check(second && pump() == 0.0F);
    }
    reached("midi: the selected track plays");

    // Arming from the rendered button.
    {
        auto* button = named("recordButton");
        check(button != nullptr && !button->property("armed").toBool());
        if (button != nullptr)
            click_at(button, {button->width() / 2, button->height() / 2},
                     Qt::LeftButton);
        lay_out();
        check(controller.recordArmed()
           && button != nullptr && button->property("armed").toBool());
    }
    reached("midi: recording is armed from the rendered button");

    // Plays a key into the running song for `blocks` callbacks from
    // the start of `step`, and says where the engine was when the key
    // went down and came up. `release` false leaves it held.
    struct Played {
        std::uint64_t pressed = 0;
        std::uint64_t released = 0;
    };
    const auto play_into = [&](int step, int key, int blocks, bool release) {
        Played played;
        controller.seekToStep(step);
        if (!transport.playing()) controller.togglePlayback();
        (void)pump();
        played.pressed = controller.engine()->sample_position();
        check(send(0x90, key, 110));
        for (int block = 0; block < blocks; ++block) check(pump() > 0.05F);
        played.released = controller.engine()->sample_position();
        if (release) {
            check(send(0x80, key, 0));
            (void)pump();
        }
        return played;
    };
    const auto empty_step = [&pattern](int from) {
        for (int step = from; step < PatternModel::step_count; ++step)
            if (!pattern.hasStep(step)) return step;
        return -1;
    };

    // The take lands on the step it was played at, while the song is
    // still running, in every editor.
    song.selectPattern(0);
    const int step = empty_step(2);
    check(step >= 0);
    // How loud track 0 is over the first two blocks of a step, played
    // from its start: a seek into the middle of a note does not
    // strike it, so the step is heard from its beginning.
    const auto track_zero_over_step = [&](int from) {
        controller.seekToStep(from);
        controller.togglePlayback();
        float loudest = 0.0F;
        for (int block = 0; block < 2; ++block) {
            (void)pump();
            loudest = std::max(loudest, controller.engine()->track_peak(0));
        }
        controller.togglePlayback();
        (void)pump();
        return loudest;
    };
    {
        // Before: nothing sounds on track 0 in that step.
        const bool silent_before = track_zero_over_step(step) == 0.0F;

        const auto played = play_into(step, 64, 12, true);
        // The interface polls the engine; the song keeps playing.
        settle(120);
        lay_out();
        check(transport.playing() && pattern.hasStep(step));
        const auto row = pattern.steps().at(step).toMap();
        const double per_sample = 480.0 * 120.0 / (60.0 * controller.engine()->sample_rate());
        const auto pressed_tick = static_cast<int>(std::floor(played.pressed * per_sample));
        const auto released_tick = static_cast<int>(std::floor(played.released * per_sample));
        pattern.selectStep(step);
        check(row.value("key").toInt() == 64
           && pattern.selected().value("micro").toInt()
                  == pressed_tick - step * PatternModel::ticks_per_step
           && pattern.stepDuration(step) == released_tick - pressed_tick);
        check(rendered_step(step) && roll_draws(step)
           && tracker_note(step) == song.pitchName(64, 0.0));
        controller.togglePlayback();
        (void)pump();

        // The arrangement plays it back where it was played, with no
        // key down: track 0 now sounds in that step.
        const bool heard_after = track_zero_over_step(step) > 0.05F;
        check(silent_before && heard_after);
        if (!silent_before || !heard_after)
            std::cerr << "REGRESSION: recorded take not heard back (before="
                      << silent_before << " after=" << heard_after << ")\n";

        // One take, one undo.
        check(song.undo() && !pattern.hasStep(step) && !rendered_step(step));
        check(song.redo() && pattern.hasStep(step) && rendered_step(step));
    }
    reached("midi: a take is written where it was played and heard back");

    // Stopping with a key still down writes it as released there.
    {
        const int held_step = empty_step(step + 2);
        check(held_step >= 0);
        const auto played = play_into(held_step, 67, 6, false);
        controller.togglePlayback();
        const double per_sample = 480.0 * 120.0 / (60.0 * controller.engine()->sample_rate());
        check(pattern.hasStep(held_step)
           && pattern.steps().at(held_step).toMap().value("key").toInt() == 67
           && pattern.stepDuration(held_step)
                  == static_cast<int>(std::floor(played.released * per_sample))
                         - static_cast<int>(std::floor(played.pressed * per_sample)));
        const int events = pattern.rowCount();
        // The release that arrives after the stop writes nothing more.
        check(send(0x80, 67, 0));
        (void)pump();
        settle(80);
        check(pattern.rowCount() == events);
    }
    reached("midi: stopping ends the take");

    // Unarmed, a playing song records nothing.
    {
        auto* button = named("recordButton");
        if (button != nullptr)
            click_at(button, {button->width() / 2, button->height() / 2},
                     Qt::LeftButton);
        check(!controller.recordArmed());
        const int events = pattern.rowCount();
        const int spare = empty_step(0);
        check(spare >= 0);
        (void)play_into(std::max(0, spare), 72, 4, true);
        settle(80);
        controller.togglePlayback();
        check(pattern.rowCount() == events && !pattern.hasStep(spare));
    }
    reached("midi: an unarmed song records nothing");

    // In nineteen tones the controller's keys are the song's degrees,
    // and the take keeps the pitch that was heard.
    {
        const QString tuning = song.tuningName();
        song.setTuning("19-EDO");
        check(song.divisions() == 19);
        controller.toggleRecord();
        const int tuned_step = empty_step(0);
        check(tuned_step >= 0);
        (void)play_into(tuned_step, 61, 4, true);
        settle(120);
        controller.togglePlayback();
        const auto pitches = pattern.pitchesAt(tuned_step);
        const auto wanted = song.pitchForDegree(61);
        check(pitches.size() == 1 && pitches.front().key == wanted.key
           && std::abs(pitches.front().cents - wanted.cents) < 0.01
           && tracker_note(tuned_step) == song.degreeName(61));
        if (pitches.size() != 1 || pitches.front().key != wanted.key)
            std::cerr << "REGRESSION: 19-EDO take wrote the wrong pitch\n";
        song.undo();
        check(!pattern.hasStep(tuned_step));
        song.setTuning(tuning);
    }
    reached("midi: keys follow the song's tuning");

    // features/timebase.feature: a take played after a tempo change lands on
    // the tick that was heard. The song slows to 60 BPM from bar 2; a key
    // played in bar 2 is read back through the tempo map, not at 120 BPM.
    {
        check(song.setTempoPoint(1920.0, 60.0, false));
        controller.flushRecompile();
        const auto& clock = controller.engine()->published_clock();
        // By hand: bar 1 is 50 samples a tick at 48 kHz, then 100.
        check(clock.sample_at(1920) == 96000.0 && clock.sample_at(2400) == 144000.0);
        const auto tick_of = [](std::uint64_t sample) {
            return sample < 96000 ? static_cast<int>(sample / 50)
                                  : 1920 + static_cast<int>((sample - 96000) / 100);
        };
        const int slow_step = empty_step(4);
        check(slow_step >= 0);
        const auto played = play_into(16 + slow_step, 65, 8, true);
        settle(120);
        lay_out();
        // The readouts follow the song's own tempo map and meter.
        check(text_of("tempoReadout") == "60.00" && transport.bar() == 1
           && text_of("positionReadout").startsWith("2."));
        controller.togglePlayback();
        const int pressed = tick_of(played.pressed % controller.engine()->song_samples());
        const int released = tick_of(played.released % controller.engine()->song_samples());
        pattern.selectStep(slow_step);
        const bool landed = pattern.hasStep(slow_step)
            && pattern.steps().at(slow_step).toMap().value("key").toInt() == 65
            && pattern.selected().value("micro").toInt()
                   == pressed - 1920 - slow_step * PatternModel::ticks_per_step
            && pattern.stepDuration(slow_step) == released - pressed;
        check(landed && pressed >= 1920);
        if (!landed)
            std::cerr << "REGRESSION: a take after a tempo change landed on the wrong tick"
                      << " (pressed tick " << pressed << ")\n";
        // One undo takes the take back; the tempo point stays for the
        // screenshot, the transport showing the slower tempo.
        check(song.undo() && !pattern.hasStep(slow_step)
           && song.song().tempo.points.size() == 2);
        // The ruler's seek places bar 2 with the tempo map: two seconds in.
        controller.seekToBar(1);
        (void)pump();
        lay_out();
        check(text_of("tempoReadout") == "60.00" && text_of("positionReadout") == "2.1.1"
           && controller.engine()->sample_position() == 96000);

        // A 7/8 bar 2: the bar layout, the clips, the ruler seek and the
        // readout all follow the meter map, and one undo puts it back.
        const auto chorus_bar = [&song] {
            for (const auto& row : song.clips())
                if (row.toMap().value("track").toInt() == 0
                    && row.toMap().value("name").toString() == "CHORUS")
                    return row.toMap().value("bar").toInt();
            return -1;
        };
        check(chorus_bar() == 2);
        check(song.setMeter(1, 7, 8));
        const auto layout = song.barLayout();
        const auto bar_entry = [&layout](int bar) { return layout.at(bar).toMap(); };
        check(layout.size() == song.bars()
           && bar_entry(1).value("ticks").toInt() == 1680
           && bar_entry(1).value("numerator").toInt() == 7
           && bar_entry(1).value("denominator").toInt() == 8
           && bar_entry(2).value("start").toInt() == 3600
           && std::abs((bar_entry(1).value("x1").toDouble() - bar_entry(1).value("x0").toDouble())
                       / (bar_entry(0).value("x1").toDouble() - bar_entry(0).value("x0").toDouble())
                       - 7.0 / 8.0) < 1e-9);
        // The chorus kept its bar number (decision 9): it now starts at 3600.
        check(chorus_bar() == 2 && song.song().clips.at(1).start == 3600);
        controller.seekToBar(2);
        (void)pump();
        lay_out();
        // Tick 3600: 1920 ticks at 50 samples, then 1680 at 100.
        check(text_of("positionReadout") == "3.1.1"
           && controller.engine()->sample_position() == 96000 + 168000);
        check(song.undo() && song.song().meter == blokkily::MeterMap{} && chorus_bar() == 2
           && song.song().clips.at(1).start == 3840);
        controller.seekToBar(1);
        (void)pump();
        lay_out();
    }
    reached("midi: a take across a tempo change lands where it was played");

    // Left armed with the take in view, and the panel naming the port
    // and the last key, for the screenshot.
    check(send(0x90, 64, 96) && send(0x80, 64, 0));
    (void)pump();
    (void)pump();
    settle(80);
    lay_out();
    check(text_of("midiActivity").endsWith(" · 96"));
    reached("midi: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    std::ostringstream details;
    details << " | midi=" << controller.midiPort().toStdString()
            << " | notes=" << controller.midiNotes()
            << " | armed=" << controller.recordArmed()
            << " | events=" << pattern.rowCount();
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("midi", run_midi);

}  // namespace
}  // namespace blokkily::verify
