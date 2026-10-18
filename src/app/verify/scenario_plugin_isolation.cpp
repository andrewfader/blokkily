// features/plugin_isolation.feature, run by bdd_plugin_isolation with
// --isolate-plugins: the window loads the test synth and a synth that crashes
// on cue, each in a helper process of its own. The crash is played from the
// song; the window, the song and the other track play on, the banner names
// the plugin, and RELOAD brings it back, playing again from the state it had.
#include "verify/harness.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_plugin_isolation(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "plugin isolation failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    const auto settle = [&](int ms) { VerifyContext::settle(ms); layout(); };
    const auto click = [&](const char* name) {
        auto* item = ctx.named(QString::fromLatin1(name));
        if (item == nullptr || !item->isVisible()) return false;
        ctx.click_at(item, {item->width() / 2, item->height() / 2}, Qt::LeftButton);
        layout();
        return true;
    };
    // The loudest each track gets over a few blocks of the song playing.
    const auto play = [&](int blocks) {
        std::vector<float> loudest(song.trackCount(), 0.0F);
        c.seekToStep(0);
        c.togglePlayback();
        for (int block = 0; block < blocks; ++block) {
            (void)ctx.pump();
            for (int track = 0; track < song.trackCount(); ++track)
                if (c.engine())
                    loudest[track] = std::max(loudest[track], c.engine()->track_peak(track));
        }
        c.togglePlayback();
        (void)ctx.pump();
        return loudest;
    };

    check(c.pluginIsolation() && c.pluginIsolationAvailable());
    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    const QString faulty = ctx.parser.value("faulty-clap");
    check(!faulty.isEmpty());
    c.scanPluginPaths({ctx.parser.value("clap-fixture").toStdString(), faulty.toStdString()}, {}, {});
    layout();
    // Track 0 plays the test synth; track 1 is given the faulty one from the
    // browser.
    check(song.trackCount() >= 2);
    song.selectTrack(1);
    layout();
    check(click("pluginFilter"));
    for (const QChar letter : QStringLiteral("faulty"))
        ctx.type_key(static_cast<Qt::Key>(Qt::Key_A + (letter.unicode() - 'a')), QString(letter));
    ctx.type_key(Qt::Key_Return, QStringLiteral("\r"));
    ctx.type_key(Qt::Key_Escape, QString());
    c.flushRecompile();
    settle(100);
    const auto& tracks = song.song().tracks;
    check(tracks.size() >= 2 && tracks[1].instrument.identifier == "dev.blokkily.faulty");
    // Both in helpers: each reports the block of latency the engine
    // compensates, and nothing has failed.
    auto* lead = c.engine() ? c.engine()->processor(track_instrument(0)) : nullptr;
    auto* faulty_synth = c.engine() ? c.engine()->processor(track_instrument(1)) : nullptr;
    check(lead && faulty_synth && lead->latency_samples() >= 512 && faulty_synth->latency_samples() >= 512);
    check(c.engine() && c.engine()->output_latency() >= 512);
    // A note on each track's step 0.
    song.selectTrack(0);
    pattern.setStepKey(0, 60);
    song.selectTrack(1);
    pattern.setStepKey(0, 64);
    c.flushRecompile();
    layout();
    const auto before = play(16);
    std::cerr << "before: " << before[0] << ", " << before[1] << '\n';
    check(before.size() >= 2 && before[0] > 0.01F && before[1] > 0.01F);
    check(c.failedPlugins().isEmpty() && !ctx.named("pluginFailureBanner")->isVisible());
    ctx.reached("plugin isolation: both synths play from processes of their own");

    // Key 127 crashes the faulty synth's process, as a plugin's own bug would.
    pattern.setStepKey(0, 127);
    c.flushRecompile();
    (void)play(16);
    settle(600);
    std::cerr << "failed: " << c.failedPlugins().join(", ").toStdString() << '\n';
    check(c.failedPlugins().size() == 1 && c.failedPlugins().front().contains("blokkily-faulty on"));
    auto* banner = ctx.named("pluginFailureBanner");
    check(banner && banner->isVisible() && VerifyContext::usable(banner, 300, 24));
    check(VerifyContext::usable(ctx.named("reloadPlugins"), 40, 18));
    const auto during = play(16);
    std::cerr << "after the crash: " << during[0] << ", " << during[1] << '\n';
    check(during[0] > 0.01F && during[1] == 0.0F);
    check(c.status().contains("stopped responding"));
    if (ctx.parser.isSet("screenshot")) {
        QString path = ctx.parser.value("screenshot");
        check(ctx.save_screenshot_to(path.left(path.size() - 4) + QStringLiteral("-crashed.png")));
    }
    ctx.reached("plugin isolation: the crash silences its track; the window and the other track play on");

    // RELOAD: the synth is made again from its last state and plays.
    pattern.setStepKey(0, 64);
    c.flushRecompile();
    check(click("reloadPlugins"));
    settle(100);
    check(c.failedPlugins().isEmpty() && !ctx.named("pluginFailureBanner")->isVisible());
    const auto after = play(16);
    std::cerr << "after RELOAD: " << after[0] << ", " << after[1] << '\n';
    check(after[0] > 0.01F && after[1] > 0.01F);
    ctx.reached("plugin isolation: RELOAD brings the plugin back, and it plays");

    // The switch in SETTINGS: off, the plugins load in the window (no block
    // of latency) and still play.
    check(click("viewSETTINGS"));
    check(VerifyContext::usable(ctx.named("pluginIsolation"), 40, 18));
    check(click("pluginIsolation"));
    check(!c.pluginIsolation());
    lead = c.engine() ? c.engine()->processor(track_instrument(0)) : nullptr;
    check(lead && lead->latency_samples() < 512);
    check(play(16)[0] > 0.01F);
    check(click("pluginIsolation"));
    check(c.pluginIsolation());
    check(click("viewSETTINGS"));
    ctx.reached("plugin isolation: the SETTINGS switch moves plugins in and out of their processes");

    check(ctx.save_screenshot());
    ctx.finish("a crashing plugin isolated, silenced, and reloaded");
}

[[maybe_unused]] const bool registered = register_scenario("plugin_isolation", run_plugin_isolation);

} // namespace
} // namespace blokkily::verify
