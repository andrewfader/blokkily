// features/chord_velocity.feature, run on its own by the bdd_chord_velocity
// gate (`--scenario chords`). Two keys of a MIDI keyboard are recorded onto one
// step, struck at different velocities and held for different times. The step
// becomes a chord that keeps both, the tracker and the inspector show them,
// dragging one voice's bar in the rendered inspector changes that voice alone
// — heard through the production render callback on the real CLAP fixture —
// and the edit is one step of history and survives a save and a load.

#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QVariantList>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>

namespace blokkily::verify {
namespace {

constexpr int soft = 40;     // MIDI velocity of the lower key
constexpr int hard = 120;    // MIDI velocity of the upper key
constexpr int lower_key = 60;
constexpr int upper_key = 64;
// The CLAP fixture in its velocity mode sounds 0.25 × the sum of the held
// velocities; the track is panned hard left so the left channel is unity.
constexpr double fixture_level = 0.25;

QVariantList selected_list(PatternModel& pattern, const char* key) {
    return pattern.selected().value(QString::fromLatin1(key)).toList();
}

void run_chords(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    auto& controller = ctx.controller;
    const auto& parser = ctx.parser;
    const auto check = [&ctx](bool ok) { ctx.check(ok); };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto settle = [](int milliseconds) { VerifyContext::settle(milliseconds); };
    const auto pump = [&ctx] { return ctx.pump(); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    auto& midi = controller.midiInput();
    const auto send = [&midi](std::uint8_t status, int key, int velocity) {
        const std::array<std::uint8_t, 3> message{
            status, static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(velocity)};
        return midi.inject(message);
    };

    // The CLAP fixture on track 0, behind the production adapter, played
    // from a deterministic MIDI input.
    check(controller.verifyClap(parser.value("clap-fixture")));
    check(controller.engine() != nullptr &&
          song.song().tracks.at(0).instrument.format == "CLAP");
    controller.setTempo(120.0);
    song.selectTrack(0);
    song.selectPattern(0);
    song.setTrackPan(0, -1.0);
    check(window->setProperty("view", "ALL"));
    controller.refreshMidiPorts();
    check(controller.selectMidiPort(0) && controller.midiPort() == "Deterministic input");
    lay_out();
    reached("chords: instruments");

    // An empty step with an empty step after it, so nothing else sounds
    // while the chord is measured from its start.
    int step = -1;
    for (int candidate = 2; candidate + 1 < pattern.stepCount(); ++candidate)
        if (!pattern.hasStep(candidate) && !pattern.hasStep(candidate + 1)) {
            step = candidate;
            break;
        }
    check(step >= 0);
    step = std::max(step, 0);

    // Two keys go down together, soft and hard; the soft one comes up first.
    controller.toggleRecord();
    check(controller.recordArmed());
    controller.seekToStep(step);
    controller.togglePlayback();
    (void)pump();
    check(send(0x90, lower_key, soft) && send(0x90, upper_key, hard));
    for (int block = 0; block < 3; ++block) (void)pump();
    check(send(0x80, lower_key, 0));
    for (int block = 0; block < 7; ++block) (void)pump();
    check(send(0x80, upper_key, 0));
    (void)pump();
    settle(120);
    controller.togglePlayback();
    (void)pump();
    controller.toggleRecord();
    check(!controller.recordArmed());
    settle(40);
    pattern.selectStep(step);
    lay_out();
    const auto units = selected_list(pattern, "voiceUnits");
    const auto lengths = selected_list(pattern, "voiceLengths");
    const bool merged = pattern.selected().value("voices").toInt() == 2 && units.size() == 2 &&
                        units.at(0).toInt() == soft && units.at(1).toInt() == hard &&
                        lengths.size() == 2 && lengths.at(0).toInt() > 0 &&
                        lengths.at(1).toInt() > lengths.at(0).toInt() * 2;
    check(merged);
    if (!merged)
        std::cerr << "REGRESSION: the merged step lost its voices' velocity or length (voices="
                  << pattern.selected().value("voices").toInt() << " units="
                  << (units.size() > 0 ? units.at(0).toInt() : -1) << ','
                  << (units.size() > 1 ? units.at(1).toInt() : -1) << " lengths="
                  << (lengths.size() > 0 ? lengths.at(0).toInt() : -1) << ','
                  << (lengths.size() > 1 ? lengths.at(1).toInt() : -1) << ")\n";
    // The tracker's VEL column reads the chord's loudest voice.
    check(pattern.steps().at(step).toMap().value("velocityUnits").toInt() == hard);
    reached("chords: a take merged onto a step keeps each voice's velocity and length");

    // The fixture's velocity mode, as a parameter lock on the step, so the
    // step's audio is the sum of its voices' velocities.
    pattern.setSelectedLock(2, 1.0, false);
    // How loud track 0 is over the start of the step, played from its start.
    const auto chord_peak = [&] {
        controller.seekToStep(step);
        controller.togglePlayback();
        float loudest = 0.0F;
        for (int block = 0; block < 4; ++block) {
            (void)pump();
            loudest = std::max(loudest, controller.engine()->track_peak(0));
        }
        controller.togglePlayback();
        (void)pump();
        return static_cast<double>(loudest);
    };
    const double recorded_peak = chord_peak();
    const double recorded_expected = fixture_level * (soft + hard) / 127.0;
    check(std::abs(recorded_peak - recorded_expected) < 1e-4);
    reached("chords: the chord is heard at its voices' velocities");

    // The editors scroll; the inspector sits below the keyboard, so it is
    // scrolled into the window the way a producer scrolls to it, and each bar
    // must then lie inside the window, where a pointer can reach it.
    const auto show_inspector = [&] {
        lay_out();
        if (auto* scroll = ctx.named("editorScroll")) {
            scroll->setProperty("contentY", std::max(0.0,
                scroll->property("contentHeight").toDouble() - scroll->height()));
            QCoreApplication::processEvents();
        }
        lay_out();
    };
    const auto on_screen = [window](const QQuickItem* item) {
        if (item == nullptr) return false;
        const auto top_left = item->mapToScene({0, 0});
        return top_left.y() >= 0 && top_left.x() >= 0 &&
               top_left.y() + item->height() <= window->height() &&
               top_left.x() + item->width() <= window->width();
    };
    show_inspector();

    // The inspector shows one usable bar per voice.
    auto* bar = ctx.named("voiceVel0");
    auto* upper_bar = ctx.named("voiceVel1");
    check(VerifyContext::usable(bar, 18, 48) && VerifyContext::usable(upper_bar, 18, 48));
    check(on_screen(bar) && on_screen(upper_bar));
    check(VerifyContext::usable(ctx.named("chordVoices"), 40, 60));
    reached("chords: the inspector shows each voice");

    // Dragging the soft voice's bar to the top is one gesture that raises
    // that voice alone, heard through the render callback.
    if (bar != nullptr) {
        const double x = bar->width() / 2;
        ctx.mouse_at(bar, {x, bar->height() / 2}, QEvent::MouseButtonPress, Qt::LeftButton,
                     Qt::LeftButton);
        ctx.mouse_at(bar, {x, bar->height() / 4}, QEvent::MouseMove, Qt::NoButton,
                     Qt::LeftButton);
        ctx.mouse_at(bar, {x, -4.0}, QEvent::MouseMove, Qt::NoButton, Qt::LeftButton);
        ctx.mouse_at(bar, {x, -4.0}, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
    }
    lay_out();
    const auto dragged = selected_list(pattern, "voiceUnits");
    check(dragged.size() == 2 && dragged.at(0).toInt() == 127 && dragged.at(1).toInt() == hard);
    const double dragged_peak = chord_peak();
    const double dragged_expected = fixture_level * (1.0 + hard / 127.0);
    check(std::abs(dragged_peak - dragged_expected) < 1e-4);
    if (std::abs(dragged_peak - dragged_expected) >= 1e-4)
        std::cerr << "chord peak after the drag " << dragged_peak << ", wanted "
                  << dragged_expected << " (voice units "
                  << (dragged.size() > 0 ? dragged.at(0).toInt() : -1) << ','
                  << (dragged.size() > 1 ? dragged.at(1).toInt() : -1) << ")\n";
    reached("chords: dragging one voice changes only that voice");

    // One drag is one step of history.
    check(song.undo());
    lay_out();
    const auto undone = selected_list(pattern, "voiceUnits");
    check(undone.size() == 2 && undone.at(0).toInt() == soft && undone.at(1).toInt() == hard);
    check(std::abs(chord_peak() - recorded_expected) < 1e-4);
    check(song.redo());
    lay_out();
    const auto redone = selected_list(pattern, "voiceUnits");
    check(redone.size() == 2 && redone.at(0).toInt() == 127 && redone.at(1).toInt() == hard);
    reached("chords: undo and redo");

    // Saved and loaded, the chord keeps each voice.
    const QString project = parser.value("project");
    if (!project.isEmpty()) {
        check(controller.saveProjectFile(project));
        song.undo();   // the loaded file, not the song in memory, must bring it back
        check(controller.loadProjectFile(project));
        settle(40);
        pattern.selectStep(step);
        lay_out();
        const auto loaded = selected_list(pattern, "voiceUnits");
        const auto loaded_lengths = selected_list(pattern, "voiceLengths");
        check(loaded.size() == 2 && loaded.at(0).toInt() == 127 && loaded.at(1).toInt() == hard &&
              loaded_lengths == lengths);
        check(controller.engine() != nullptr &&
              std::abs(chord_peak() - dragged_expected) < 1e-4);
    }
    reached("chords: save and load");

    // Left with the chord selected, its voices in the inspector.
    controller.seekToStep(0);
    pattern.selectStep(step);
    settle(40);
    show_inspector();
    check(on_screen(ctx.named("voiceVel0")));
    reached("chords: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    std::ostringstream details;
    details << " | step=" << step << " | recorded=" << recorded_peak
            << " | dragged=" << dragged_peak << " (want " << dragged_expected << ")";
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("chords", run_chords);

}  // namespace
}  // namespace blokkily::verify
