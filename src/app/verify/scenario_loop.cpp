// features/arrangement_loop.feature, run by the bdd_arrangement_loop gate
// (`--scenario arrangement-loop`). LOOP, IN and OUT are pressed on the
// rendered arrangement; what is heard is the production render callback
// through the CLAP fixture (0.25 DC while a note is held). An export is
// read back from the file.

#include "verify/harness.hpp"

#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace blokkily::verify {
namespace {

constexpr std::size_t bar_samples = 96000;

blokkily::Song loop_song() {
    blokkily::Pattern hit(1920, 480);
    blokkily::Trigger trigger;
    trigger.start = 0;
    trigger.duration = 240;
    trigger.musical_data = blokkily::Note{60, 1.0F, 0.0F};
    (void)hit.add(trigger);
    blokkily::Pattern hook(1920, 480);
    trigger.musical_data = blokkily::Note{64, 1.0F, 0.0F};
    (void)hook.add(trigger);
    blokkily::Song song;
    song.patterns = {{"HIT", std::move(hit)}, {"HOOK", std::move(hook)}};
    song.tracks = {blokkily::Track{}};
    song.tracks[0].name = "LEAD";
    song.clips = {{0, 0, 0, 1}, {0, 1, 3840, 1}};
    return song;
}

void run_loop(VerifyContext& ctx) {
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
    const auto press = [&ctx, &lay_out](const char* name) {
        auto* item = ctx.named(QString::fromLatin1(name));
        if (item == nullptr) return false;
        ctx.click_at(item, {item->width() / 2, item->height() / 2}, Qt::LeftButton);
        lay_out();
        return true;
    };
    std::vector<float> stereo(2048, 0.0F);
    const auto listen = [&](std::size_t blocks) {
        std::vector<float> left;
        for (std::size_t block = 0; block < blocks; ++block) {
            controller.flushRecompile();
            std::fill(stereo.begin(), stereo.end(), 0.0F);
            if (ctx.output == nullptr || !ctx.output->pump(stereo)) break;
            left.insert(left.end(), stereo.begin(), stereo.begin() + 1024);
        }
        return left;
    };
    const auto peak_at = [](const std::vector<float>& samples, std::size_t at,
                            std::size_t length = 3000) {
        if (at + length > samples.size()) return 0.0F;
        float loudest = 0.0F;
        for (std::size_t index = 0; index < length; ++index)
            loudest = std::max(loudest, std::abs(samples[at + index]));
        return loudest;
    };

    song.replace(loop_song());
    check(controller.verifyClap(parser.value("clap-fixture")));
    check(controller.engine() != nullptr);
    controller.setTempo(120.0);
    song.selectTrack(0);
    check(window->setProperty("view", "ALL"));
    controller.seekToBar(0);
    lay_out();

    check(VerifyContext::usable(ctx.named("loopButton"), 40, 18) &&
          VerifyContext::usable(ctx.named("loopInButton"), 28, 18) &&
          VerifyContext::usable(ctx.named("loopOutButton"), 28, 18));
    check(!song.loopEnabled());
    reached("arrangement-loop: LOOP, IN and OUT sit on the arrangement, each usable");

    controller.seekToBar(2);
    lay_out();
    check(transport.bar() == 2);
    check(press("loopInButton"));
    controller.seekToBar(3);
    lay_out();
    check(press("loopOutButton"));
    check(song.loopEnabled() && song.loopStartBar() == 2 && song.loopEndBar() == 3);
    auto* const in_bar = ctx.named("rulerBar2");
    check(in_bar != nullptr && in_bar->property("inLoop").toBool());
    auto* const out_bar = ctx.named("rulerBar3");
    check(out_bar == nullptr || !out_bar->property("inLoop").toBool());
    reached("arrangement-loop: IN and OUT mark bar 3 as the loop");

    controller.seekToBar(2);
    check(press("playButton"));
    check(transport.playing());
    const auto heard = listen(200); // a little over two bars of 1024-frame pumps
    check(press("playButton"));
    (void)ctx.pump();
    check(peak_at(heard, 0) > 0.05F);
    check(peak_at(heard, bar_samples) > 0.05F);
    check(peak_at(heard, 20000) < 1e-3F);
    reached("arrangement-loop: playback from bar 3 hears that bar twice");

    const QString export_path = parser.value("export");
    check(!export_path.isEmpty());
    check(controller.exportAudioFile(export_path, "FLOAT32"));
    std::string error;
    const auto wave = blokkily::read_wave(export_path.toStdString(), &error);
    check(wave.has_value() && wave->channels == 2 && wave->frames >= 3 * bar_samples);
    if (wave) {
        std::vector<float> left(static_cast<std::size_t>(std::min<std::uint64_t>(wave->frames, 3 * bar_samples)));
        for (std::size_t frame = 0; frame < left.size(); ++frame)
            left[frame] = wave->interleaved[frame * 2];
        check(peak_at(left, 0) > 0.05F);
        check(peak_at(left, 2 * bar_samples) > 0.05F);
    }
    reached("arrangement-loop: an export still begins with bar 1's note");

    const QString project_path = parser.value("project");
    check(!project_path.isEmpty() && controller.saveProjectFile(project_path));
    const auto kept = song.song().loop;
    song.setLoopEnabled(false);
    check(!song.loopEnabled());
    check(controller.loadProjectFile(project_path));
    lay_out();
    check(song.song().loop == kept && song.loopEnabled() && song.loopStartBar() == 2);
    reached("arrangement-loop: the loop survives a save and a load");

    controller.seekToBar(2);
    lay_out();
    check(ctx.save_screenshot());
    ctx.finish(" | loop " + std::to_string(song.loopStartBar() + 1) + "-" +
               std::to_string(song.loopEndBar() + 1));
}

[[maybe_unused]] const bool registered = register_scenario("arrangement-loop", run_loop);

} // namespace
} // namespace blokkily::verify
