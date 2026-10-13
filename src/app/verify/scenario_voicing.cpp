// features/chord_voicing.feature, run on its own by the bdd_chord_voicing gate
// (`--scenario voicing`). A C4 E4 G4 chord is written on one step. The
// rendered inspector's STRUM dial is dragged and its inversion chips are
// clicked. Each edit is read back from every editor's projection, and from
// audio rendered through the production callback by the real CLAP fixture in
// its tone mode. In that mode the fixture plays a sine at the last key it was
// sent, so a strummed chord is heard voice by voice: which pitch, and when.

#include "verify/harness.hpp"
#include "../../tests/support/audio_probe.hpp"

#include <QCoreApplication>
#include <QVariantList>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <span>
#include <vector>

namespace blokkily::verify {
namespace {

constexpr double rate = 48000.0;
constexpr std::size_t block = 1024;
// 120 BPM and 480 ticks a beat at 48 kHz: one tick is 50 samples.
constexpr std::size_t samples_per_tick = 50;
constexpr int strum_ticks = 60;
constexpr std::size_t strum_samples = strum_ticks * samples_per_tick;   // 3000
constexpr int chord_ticks = 480;

double hz(int key) { return 440.0 * std::pow(2.0, (key - 69) / 12.0); }

QVariantList keys_of(PatternModel& pattern, int step) {
    return pattern.steps().at(step).toMap().value("voiceKeys").toList();
}

bool keys_are(const QVariantList& keys, std::initializer_list<int> wanted) {
    if (keys.size() != static_cast<qsizetype>(wanted.size())) return false;
    qsizetype index = 0;
    for (const int key : wanted)
        if (keys.at(index++).toInt() != key) return false;
    return true;
}

std::string describe(const QVariantList& keys) {
    std::ostringstream out;
    out << '{';
    for (qsizetype index = 0; index < keys.size(); ++index)
        out << (index ? ", " : "") << keys.at(index).toInt();
    out << '}';
    return out.str();
}

void run_voicing(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    auto& controller = ctx.controller;
    const auto& parser = ctx.parser;
    const auto check = [&ctx](bool ok) { ctx.check(ok); };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    std::ostringstream details;

    // The CLAP fixture on track 0, soloed so nothing else reaches the bus.
    check(controller.verifyClap(parser.value("clap-fixture")));
    check(controller.engine() != nullptr &&
          song.song().tracks.at(0).instrument.format == "CLAP");
    controller.setTempo(120.0);
    song.selectTrack(0);
    song.selectPattern(0);
    song.toggleSolo(0);
    check(window->setProperty("view", "ALL"));

    // The chord's step and the four after it are cleared, so the chord sounds
    // alone for its whole length.
    const int step = 0;
    check(pattern.stepCount() >= 5);
    for (int clear = step; clear < step + 5; ++clear) pattern.clearStep(clear);
    check(!pattern.hasStep(step) && !pattern.hasStep(step + 4));

    // A single note first: the voicing controls are there but disabled.
    pattern.setStepKey(step, 60);
    pattern.selectStep(step);
    lay_out();
    auto* up = ctx.named("invertUp");
    auto* down = ctx.named("invertDown");
    auto* strum = ctx.named("strumDial");
    check(up != nullptr && down != nullptr && strum != nullptr);
    const bool note_disabled = up != nullptr && !up->isEnabled() && down != nullptr &&
                               !down->isEnabled() && strum != nullptr &&
                               !strum->property("enabled").toBool();
    check(note_disabled);
    if (!note_disabled) std::cerr << "voicing: the voicing controls are enabled for a single note\n";

    // C4 E4 G4, every voice held for the chord's length, in tone mode.
    pattern.addRollNote(step, 64);
    pattern.addRollNote(step, 67);
    for (const int key : {60, 64, 67}) pattern.resizeRollNote(step, key, chord_ticks);
    pattern.selectStep(step);
    pattern.setSelectedLock(1, 1.0, false);
    check(keys_are(keys_of(pattern, step), {60, 64, 67}));
    if (!keys_are(keys_of(pattern, step), {60, 64, 67}))
        std::cerr << "voicing: the chord was written as " << describe(keys_of(pattern, step)) << '\n';

    // The editors scroll; the inspector is scrolled into the window the way
    // a producer scrolls to it, and its controls must then lie inside it.
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
    up = ctx.named("invertUp");
    down = ctx.named("invertDown");
    auto* strum_slider = ctx.named("strumDialSlider");
    check(VerifyContext::usable(up, 40, 18) && VerifyContext::usable(down, 40, 18) &&
          VerifyContext::usable(strum_slider, 80, 12));
    check(on_screen(up) && on_screen(down) && on_screen(strum_slider));
    check(up != nullptr && up->isEnabled() && down != nullptr && down->isEnabled() &&
          strum_slider != nullptr && strum_slider->isEnabled());
    check(VerifyContext::usable(ctx.named("chordVoices"), 40, 60));
    reached("voicing: the inspector offers inversion and strum for a chord");

    // The step played from its start: the left channel of the bus, from the
    // first sample the chord sounds.
    const auto play = [&] {
        controller.seekToStep(step);
        controller.togglePlayback();
        std::vector<float> left;
        std::vector<float> stereo(block * 2);
        const std::size_t frames = static_cast<std::size_t>(chord_ticks) * samples_per_tick;
        while (left.size() < frames + block) {
            controller.flushRecompile();
            std::fill(stereo.begin(), stereo.end(), 0.0F);
            if (ctx.output == nullptr || !ctx.output->pump(stereo)) break;
            left.insert(left.end(), stereo.begin(),
                        stereo.begin() + static_cast<std::ptrdiff_t>(block));
        }
        controller.togglePlayback();
        (void)ctx.pump();
        const auto onset = std::find_if(left.begin(), left.end(),
                                        [](float sample) { return std::abs(sample) > 1e-4F; });
        left.erase(left.begin(), onset);
        return left;
    };
    // The pitch heard over [from, from + length) samples after the onset.
    const auto pitch_at = [](const std::vector<float>& heard, std::size_t from,
                             std::size_t length) {
        if (from + length > heard.size()) return 0.0;
        return probe::dominant_frequency(std::span<const float>(heard).subspan(from, length),
                                         rate, 150.0, 700.0, 1.0);
    };
    // Each voice, in turn, strum_samples after the one before it; the last
    // one holds on.
    const auto heard_in_order = [&](std::initializer_list<int> keys, const char* what) {
        const auto heard = play();
        bool ok = true;
        std::size_t index = 0;
        details << " | " << what << ":";
        for (const int key : keys) {
            const double got = pitch_at(heard, index * strum_samples + 400, 2200);
            details << ' ' << static_cast<int>(std::lround(got));
            ok = ok && std::abs(got - hz(key)) < 0.03 * hz(key);
            ++index;
        }
        if (!ok) std::cerr << "voicing: " << what << " was not heard voice by voice in order\n";
        return ok;
    };

    // Unstrummed, every voice starts together and the last one is heard.
    {
        const auto heard = play();
        const double got = pitch_at(heard, 400, 2200);
        check(std::abs(got - hz(67)) < 0.03 * hz(67));
        details << " | unstrummed " << std::lround(got);
    }

    // The STRUM dial dragged to its end is one gesture: 60 ticks.
    if (strum_slider != nullptr) {
        const double y = strum_slider->height() / 2;
        ctx.mouse_at(strum_slider, {strum_slider->width() / 2, y}, QEvent::MouseButtonPress,
                     Qt::LeftButton, Qt::LeftButton);
        ctx.mouse_at(strum_slider, {strum_slider->width() - 2, y}, QEvent::MouseMove,
                     Qt::NoButton, Qt::LeftButton);
        ctx.mouse_at(strum_slider, {strum_slider->width() + 20, y}, QEvent::MouseMove,
                     Qt::NoButton, Qt::LeftButton);
        ctx.mouse_at(strum_slider, {strum_slider->width() + 20, y},
                     QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
    }
    lay_out();
    check(pattern.selected().value("strum").toInt() == strum_ticks);
    check(strum != nullptr && strum->property("readout").toString() == "60 TK");
    check(heard_in_order({60, 64, 67}, "strummed"));
    check(song.undo());
    check(pattern.selected().value("strum").toInt() == 0);
    check(song.redo());
    check(pattern.selected().value("strum").toInt() == strum_ticks);
    reached("voicing: strum spreads a chord's voices out in time, lowest first");

    // INV ▲: E4 G4 C5, in the tracker, the piano roll and the audio.
    show_inspector();
    if (up != nullptr) ctx.click_at(up, {up->width() / 2, up->height() / 2}, Qt::LeftButton);
    lay_out();
    const auto lifted = keys_of(pattern, step);
    check(keys_are(lifted, {64, 67, 72}));
    if (!keys_are(lifted, {64, 67, 72}))
        std::cerr << "voicing: INV up gave " << describe(lifted) << ", wanted {64, 67, 72}\n";
    check(pattern.steps().at(step).toMap().value("noteName").toString() == "E4+2");
    check(heard_in_order({64, 67, 72}, "first inversion"));

    // INV ▼ twice: back through root position to G3 C4 E4.
    show_inspector();
    for (int click = 0; click < 2 && down != nullptr; ++click) {
        ctx.click_at(down, {down->width() / 2, down->height() / 2}, Qt::LeftButton);
        lay_out();
    }
    const auto dropped = keys_of(pattern, step);
    check(keys_are(dropped, {55, 60, 64}));
    if (!keys_are(dropped, {55, 60, 64}))
        std::cerr << "voicing: INV down twice gave " << describe(dropped)
                  << ", wanted {55, 60, 64}\n";
    check(pattern.steps().at(step).toMap().value("noteName").toString() == "G3+2");
    check(heard_in_order({55, 60, 64}, "inverted down"));

    // Each click is one step of history.
    check(song.undo());
    check(keys_are(keys_of(pattern, step), {60, 64, 67}));
    check(song.undo());
    check(keys_are(keys_of(pattern, step), {64, 67, 72}));
    check(song.redo() && song.redo());
    check(keys_are(keys_of(pattern, step), {55, 60, 64}));
    reached("voicing: inverting a chord turns its voices in every editor and in the audio");

    // Saved and loaded, the chord keeps its voicing and its strum.
    const QString project = parser.value("project");
    check(!project.isEmpty());
    if (!project.isEmpty()) {
        check(controller.saveProjectFile(project));
        check(song.undo());   // the loaded file, not the song in memory, must bring it back
        check(controller.loadProjectFile(project));
        VerifyContext::settle(40);
        pattern.selectStep(step);
        lay_out();
        check(keys_are(keys_of(pattern, step), {55, 60, 64}));
        check(pattern.selected().value("strum").toInt() == strum_ticks);
        check(controller.engine() != nullptr);
        if (!song.song().tracks.at(0).mix.solo) song.toggleSolo(0);
        check(heard_in_order({55, 60, 64}, "loaded"));
    }
    reached("voicing: a voiced chord survives a save and a load");

    // Left with the chord selected, its voicing in the inspector.
    controller.seekToStep(0);
    pattern.selectStep(step);
    VerifyContext::settle(40);
    show_inspector();
    check(on_screen(ctx.named("invertUp")));
    reached("voicing: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    details << " | step=" << step;
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("voicing", run_voicing);

}  // namespace
}  // namespace blokkily::verify
