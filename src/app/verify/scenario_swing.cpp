// features/pattern_swing.feature, run by the bdd_pattern_swing gate
// (`--scenario pattern-swing`). SWING is pressed on the rendered pattern
// header; what is heard is the production render callback through the CLAP
// fixture. An export is read back from the file.

#include "verify/harness.hpp"

#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace blokkily::verify {
namespace {

constexpr std::size_t sixteenth_samples = 6000;

blokkily::Song swing_song() {
    blokkily::Pattern pattern(1920, 480);
    blokkily::Trigger trigger;
    trigger.start = 0;
    trigger.duration = 60;
    trigger.musical_data = blokkily::Note{60, 1.0F, 0.0F};
    (void)pattern.add(trigger);
    trigger.start = 120;
    trigger.musical_data = blokkily::Note{64, 1.0F, 0.0F};
    (void)pattern.add(trigger);
    blokkily::Song song;
    song.patterns = {{"SWING", std::move(pattern)}};
    song.tracks = {blokkily::Track{}};
    song.tracks[0].name = "LEAD";
    song.clips = {{0, 0, 0, 1}};
    return song;
}

void run_swing(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& transport = ctx.transport;
    auto& controller = ctx.controller;
    const auto& parser = ctx.parser;
    const auto check = [&ctx](bool ok) { ctx.check(ok); };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    const auto press = [&ctx, &lay_out](const char* name, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        auto* item = ctx.named(QString::fromLatin1(name));
        if (item == nullptr) return false;
        ctx.click_at(item, {item->width() / 2, item->height() / 2}, Qt::LeftButton, modifiers);
        lay_out();
        return true;
    };
    std::vector<float> stereo(2048, 0.0F);
    const auto listen = [&](std::size_t frames) {
        std::vector<float> left;
        while (left.size() < frames) {
            controller.flushRecompile();
            std::fill(stereo.begin(), stereo.end(), 0.0F);
            if (ctx.output == nullptr || !ctx.output->pump(stereo)) break;
            left.insert(left.end(), stereo.begin(), stereo.begin() + 1024);
        }
        return left;
    };
    const auto peak_at = [](const std::vector<float>& samples, std::size_t at,
                            std::size_t length = 400) {
        if (at + length > samples.size()) return 0.0F;
        float loudest = 0.0F;
        for (std::size_t index = 0; index < length; ++index)
            loudest = std::max(loudest, std::abs(samples[at + index]));
        return loudest;
    };

    song.replace(swing_song());
    check(controller.verifyClap(parser.value("clap-fixture")));
    check(controller.engine() != nullptr);
    controller.setTempo(120.0);
    song.selectTrack(0);
    check(window->setProperty("view", "ALL"));
    controller.seekToBar(0);
    lay_out();

    check(VerifyContext::usable(ctx.named("patternSwing"), 70, 18) &&
          VerifyContext::usable(ctx.named("patternSwingUp"), 16, 16) &&
          VerifyContext::usable(ctx.named("patternSwingDown"), 16, 16));
    auto* const readout = ctx.named("patternSwingReadout");
    check(readout != nullptr && VerifyContext::reads_whole(readout, QStringLiteral("0%")));
    check(song.patternSwing() == 0);
    reached("pattern-swing: SWING sits beside the length, usable, reading 0%");

    for (int step = 0; step < 4; ++step) check(press("patternSwingUp", Qt::ShiftModifier));
    check(song.patternSwing() == 100);
    check(readout != nullptr && VerifyContext::reads_whole(readout, QStringLiteral("100%")));
    reached("pattern-swing: Shift-clicking + sets swing to 100%");

    controller.flushRecompile();
    controller.seekToBar(0);
    check(press("playButton"));
    check(transport.playing());
    const auto heard = listen(3 * sixteenth_samples);
    check(press("playButton"));
    (void)ctx.pump();
    check(peak_at(heard, 0) > 0.05F);
    check(peak_at(heard, sixteenth_samples) < 1e-3F);
    check(peak_at(heard, sixteenth_samples + sixteenth_samples / 2) > 0.05F);
    reached("pattern-swing: playback hears the second step late");

    check(song.canUndo());
    while (song.patternSwing() != 0 && song.canUndo()) check(song.undo());
    lay_out();
    check(song.patternSwing() == 0);
    reached("pattern-swing: undo puts the grid back to straight");

    while (song.patternSwing() != 100 && song.canRedo()) check(song.redo());
    lay_out();
    check(song.patternSwing() == 100);

    const QString export_path = parser.value("export");
    check(!export_path.isEmpty());
    check(controller.exportAudioFile(export_path, "FLOAT32"));
    std::string error;
    const auto wave = blokkily::read_wave(export_path.toStdString(), &error);
    check(wave.has_value() && wave->channels == 2 && wave->frames >= 3 * sixteenth_samples);
    if (wave) {
        std::vector<float> left(static_cast<std::size_t>(
            std::min<std::uint64_t>(wave->frames, 3 * sixteenth_samples)));
        for (std::size_t frame = 0; frame < left.size(); ++frame)
            left[frame] = wave->interleaved[frame * 2];
        check(peak_at(left, 0) > 0.05F);
        check(peak_at(left, sixteenth_samples) < 1e-3F);
        check(peak_at(left, sixteenth_samples + sixteenth_samples / 2) > 0.05F);
    }
    reached("pattern-swing: an export is the swung mix");

    const QString project_path = parser.value("project");
    check(!project_path.isEmpty() && controller.saveProjectFile(project_path));
    song.setPatternSwing(0);
    check(!song.patternSwing());
    check(controller.loadProjectFile(project_path));
    lay_out();
    check(song.patternSwing() == 100);
    reached("pattern-swing: the swing survives a save and a load");

    lay_out();
    check(ctx.save_screenshot());
    ctx.finish(" | swing " + std::to_string(song.patternSwing()) + "%");
}

[[maybe_unused]] const bool registered = register_scenario("pattern-swing", run_swing);

} // namespace
} // namespace blokkily::verify
