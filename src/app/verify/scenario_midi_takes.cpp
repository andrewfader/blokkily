// features/comping_and_punch.feature, run by bdd_midi_takes: MIDI passes
// recorded in a loop stack as takes in the window. The first bar is looped
// over an empty part and three passes are played, a note early in the first,
// in the middle of the second and late in the third. Each becomes a take over
// the bar, the last plays, and the clip's TAKE badge plays the next; which
// pass is heard is read from the track's meter as the bar plays through the
// production callback. With TAKES off a pass merges into the pattern, as
// before. Everything survives a save and a load.
#include "verify/harness.hpp"

#include "blokkily/audio/comping.hpp"

#include <QCoreApplication>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <source_location>
#include <vector>

namespace blokkily::verify {
namespace {

void run_midi_takes(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    auto& pattern = ctx.pattern;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "midi takes failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    check(ctx.window->setProperty("view", "ALL"));
    // Track 0's part of the open section, empty, under a loop of the first bar.
    s.selectTrack(0);
    s.selectSection(0);
    for (int step = 0; step < pattern.stepCount(); ++step) pattern.clearStep(step);
    s.setLoopStartBar(0);
    s.setLoopEndBar(1);
    c.flushRecompile();
    const auto& loop = s.song().loop;
    check(loop.active() && loop.start == 0 && loop.end > 0 && s.midiTakes());
    const auto bar_samples = c.engine() ? c.engine()->published_clock().sample_at(loop.end) : 0.0;
    // A pump is 512 frames (1024 samples, both sides).
    const int pumps_per_bar = static_cast<int>(std::ceil(bar_samples / 512.0));
    std::cerr << "a bar is " << bar_samples << " samples, " << pumps_per_bar << " blocks\n";
    // The bar played from its start: which blocks the track sounds in.
    const auto sounding = [&] {
        std::vector<int> loud;
        c.seekToStep(0);
        if (!ctx.transport.playing()) c.togglePlayback();
        for (int block = 0; block < pumps_per_bar; ++block) {
            (void)ctx.pump();
            if (c.engine() && c.engine()->track_peak(0) > 0.01F) loud.push_back(block);
        }
        c.togglePlayback();
        (void)ctx.pump();
        return loud;
    };
    // Played keys land at the start of the block they were played in, so a
    // window is read with two blocks to spare either side.
    const auto within = [](const std::vector<int>& loud, int from, int to) {
        return !loud.empty() && std::all_of(loud.begin(), loud.end(),
                                            [&](int block) { return block >= from - 2 && block <= to + 2; });
    };
    check(sounding().empty());

    // Three passes, recorded as the bar loops: a key early in the first, in
    // the middle of the second and late in the third.
    const int windows[3][2] = {{12, 40}, {72, 100}, {132, 160}};
    c.toggleRecord();
    c.seekToStep(0);
    c.togglePlayback();
    for (int pass = 0; pass < 3; ++pass)
        for (int block = 0; block < pumps_per_bar; ++block) {
            if (block == windows[pass][0]) check(c.performKey(60 + pass * 4, true));
            if (block == windows[pass][1]) c.releasePerformed();
            (void)ctx.pump();
        }
    c.togglePlayback();
    c.toggleRecord();
    c.flushRecompile();
    layout();
    std::vector<std::size_t> takes;
    for (std::size_t clip = 0; clip < s.song().clips.size(); ++clip)
        if (is_midi_take(s.song(), clip) && s.song().clips[clip].track == 0) takes.push_back(clip);
    std::cerr << "takes: " << takes.size() << '\n';
    check(takes.size() == 3);
    if (takes.size() == 3) {
        check(s.song().clips[takes[0]].comped_out && s.song().clips[takes[1]].comped_out &&
              !s.song().clips[takes[2]].comped_out);
        check(midi_take_stack(s.song(), takes[2]).size() == 3);
    }
    // The take sections are not offered as patterns.
    const auto strip = s.sections();
    const auto offered = std::count_if(strip.begin(), strip.end(), [](const QVariant& row) {
        return !row.toMap().value("take").toBool();
    });
    check(offered + 3 == strip.size());
    auto heard = sounding();
    std::cerr << "last pass plays in blocks " << (heard.empty() ? -1 : heard.front()) << "-"
              << (heard.empty() ? -1 : heard.back()) << '\n';
    check(within(heard, windows[2][0], windows[2][1] + 1));
    ctx.reached("midi takes: three looped passes are three takes, and the last plays");

    // The badge on the bar: 3/3, and a press plays the next, the first.
    auto* badge = ctx.named("midiTakeBadge0-0");
    check(badge && badge->isVisible() && VerifyContext::usable(badge, 14, 12));
    const auto cell = s.lanes().value(0).toList().value(0).toMap();
    check(cell.value("takeIndex").toInt() == 3 && cell.value("takeCount").toInt() == 3);
    if (ctx.parser.isSet("screenshot")) {
        QString path = ctx.parser.value("screenshot");
        check(ctx.save_screenshot_to(path.left(path.size() - 4) + QStringLiteral("-badge.png")));
    }
    if (badge) ctx.click_at(badge, {badge->width() / 2, badge->height() / 2}, Qt::LeftButton);
    c.flushRecompile();
    layout();
    heard = sounding();
    check(within(heard, windows[0][0], windows[0][1] + 1));
    check(s.lanes().value(0).toList().value(0).toMap().value("takeIndex").toInt() == 1);
    // One undo puts the last pass back.
    check(s.undo());
    c.flushRecompile();
    check(within(sounding(), windows[2][0], windows[2][1] + 1));
    ctx.reached("midi takes: the TAKE badge plays the next take, and undoes");

    // TAKES off: a pass merges into the pattern under it, as an overdub.
    auto* chip = ctx.named("midiTakesButton");
    check(chip && VerifyContext::usable(chip, 40, 18));
    if (chip) ctx.click_at(chip, {chip->width() / 2, chip->height() / 2}, Qt::LeftButton);
    layout();
    check(!s.midiTakes());
    const auto sections_before = s.song().sections.size();
    c.toggleRecord();
    c.seekToStep(0);
    c.togglePlayback();
    for (int block = 0; block < pumps_per_bar; ++block) {
        if (block == windows[1][0]) check(c.performKey(72, true));
        if (block == windows[1][1]) c.releasePerformed();
        (void)ctx.pump();
    }
    c.togglePlayback();
    c.toggleRecord();
    c.flushRecompile();
    check(s.song().sections.size() == sections_before);
    heard = sounding();
    const bool merged = std::any_of(heard.begin(), heard.end(), [&](int b) { return b >= windows[1][0] && b <= windows[1][1]; }) &&
                        std::any_of(heard.begin(), heard.end(), [&](int b) { return b >= windows[2][0] && b <= windows[2][1]; });
    check(merged);
    ctx.reached("midi takes: with TAKES off a pass merges, under the take that plays");

    // Saved and loaded.
    const QString file = ctx.parser.value("project");
    check(!file.isEmpty() && c.saveProjectFile(QUrl::fromLocalFile(file).toString()));
    check(c.loadProjectFile(QUrl::fromLocalFile(file).toString()));
    c.flushRecompile();
    std::size_t loaded_takes = 0, playing = 0;
    for (std::size_t clip = 0; clip < s.song().clips.size(); ++clip)
        if (is_midi_take(s.song(), clip)) {
            ++loaded_takes;
            if (!s.song().clips[clip].comped_out) ++playing;
        }
    check(loaded_takes == 3 && playing == 1 && !s.midiTakes());
    check(within(sounding(), 0, pumps_per_bar) && !sounding().empty());
    ctx.reached("midi takes: takes, which plays, and TAKES off survive a save and a load");
    check(ctx.save_screenshot());
    ctx.finish("looped MIDI passes stacked as takes");
}

[[maybe_unused]] const bool registered = register_scenario("midi_takes", run_midi_takes);

} // namespace
} // namespace blokkily::verify
