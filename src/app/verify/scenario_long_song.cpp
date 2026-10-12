// features/session_workflow.feature, "A long song stays light while it is
// mixed", run on its own by the bdd_long_song gate (`--scenario long_song`).
// A four-minute arrangement is laid out in the real window, then mixed: each
// move of a fader, a solo or a mute must leave the window's memory where it
// was and the arrangement drawn as the song now is. Before the arrangement
// read the song once per row, every cell rebuilt every lane and every bar of
// the song, and each move left about a gigabyte behind on a song this long.

#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QColor>

#include <cmath>
#include <fstream>
#include <iostream>
#include <source_location>
#include <sstream>
#include <string>

namespace blokkily::verify {
namespace {

// The window's resident memory in MiB, or -1 where the platform does not
// say (the check is made where it can be).
long resident_mib() {
#ifdef __linux__
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line))
        if (line.rfind("VmRSS:", 0) == 0) return std::stol(line.substr(6)) / 1024;
#endif
    return -1;
}

void run_long_song(VerifyContext& ctx) {
    auto& song = ctx.song;
    const auto check = [&ctx](bool ok, std::source_location at = std::source_location::current()) {
        if (!ok) std::cerr << "long_song check failed at line " << at.line() << '\n';
        ctx.check(ok);
    };
    const auto lay_out = [&ctx] {
        QCoreApplication::processEvents();
        (void)ctx.window->grabWindow();
    };
    const auto cell = [&ctx](int track, int bar) {
        return ctx.named(QStringLiteral("clip%1-%2").arg(track).arg(bar));
    };

    // ---------------------------------------------------------------------
    ctx.reached("long_song: eight tracks across 112 bars are laid out");
    constexpr int tracks = 8;
    constexpr int bars = 112;
    constexpr Tick four_bars = 4 * 1920;
    {
        Song fresh;
        fresh.patterns.clear();
        fresh.tracks.clear();
        fresh.clips.clear();
        for (int track = 0; track < tracks; ++track) {
            fresh.tracks.push_back({"TRACK " + std::to_string(track + 1), {}, {}});
            // A note in every part, so a clip is drawn as heard or unheard
            // rather than as an empty outline.
            Pattern riff(four_bars, 480);
            Trigger note;
            note.duration = 120;
            note.musical_data = Note{static_cast<std::int16_t>(48 + track), 0.8F, 0.0F};
            (void)riff.add(note);
            fresh.patterns.push_back({"RIFF", std::move(riff), 0, static_cast<std::size_t>(track)});
            for (int block = 0; block < bars / 4; ++block)
                fresh.clips.push_back({static_cast<std::size_t>(track), static_cast<std::size_t>(track),
                                       block * four_bars, 1});
        }
        song.replace(std::move(fresh));
    }
    check(ctx.window->setProperty("view", "ALL"));
    lay_out();
    check(song.trackCount() == tracks && song.bars() >= bars);
    check(cell(tracks - 1, bars - 1) != nullptr && cell(tracks - 1, bars - 1)->property("filled").toBool());

    // ---------------------------------------------------------------------
    ctx.reached("long_song: mixing it leaves the window's memory where it was");
    const long before = resident_mib();
    song.setTrackGain(1, -3.0);
    lay_out();
    song.toggleSolo(2);
    lay_out();
    // A soloed song draws every other track's clips as unheard.
    check(cell(0, 40) != nullptr && cell(2, 40) != nullptr &&
          cell(0, 40)->property("color").value<QColor>() != cell(2, 40)->property("color").value<QColor>());
    song.toggleSolo(2);
    lay_out();
    song.toggleMute(3);
    lay_out();
    song.toggleMute(3);
    lay_out();
    check(cell(0, 40) != nullptr && cell(2, 40) != nullptr &&
          cell(0, 40)->property("color").value<QColor>() == cell(2, 40)->property("color").value<QColor>());
    const long after = resident_mib();
    if (before >= 0) {
        if (after - before >= 150)
            std::cerr << "REGRESSION: five mixer moves on a long song grew the window from " << before
                      << " MiB to " << after << " MiB\n";
        check(after - before < 150);
    }

    // ---------------------------------------------------------------------
    ctx.reached("long_song: an edit to the arrangement is drawn at once");
    check(song.removeClip(5, 60));
    lay_out();
    check(cell(5, 60) != nullptr && !cell(5, 60)->property("filled").toBool());
    check(cell(5, 56) != nullptr && cell(5, 56)->property("filled").toBool());

    // ---------------------------------------------------------------------
    ctx.reached("long_song: while it plays, the editors follow the song into the next pattern");
    check(ctx.controller.verifyClap(ctx.parser.value("clap-fixture")));
    const auto clap = song.song().tracks.at(0).instrument;
    {
        Song two;
        two.patterns.clear();
        two.sections = {{"A"}, {"B"}};
        for (std::size_t section = 0; section < 2; ++section) {
            Pattern part(1920, 480);
            for (Tick step = 0; step < 16; step += 2) {
                Trigger trigger;
                trigger.start = step * 120;
                trigger.duration = 120;
                trigger.musical_data = Note{static_cast<std::int16_t>(60 + section * 7), 0.8F, 0.0F};
                (void)part.add(trigger);
            }
            two.patterns.push_back({section == 0 ? "A" : "B", std::move(part), section, 0});
        }
        two.tracks = {Track{}};
        two.tracks[0].name = "LEAD";
        two.tracks[0].instrument = clap;
        two.clips = {{0, 0, 0, 2}, {0, 1, 2 * 1920, 2}};
        song.replace(std::move(two));
    }
    VerifyContext::settle(20);
    lay_out();
    song.selectTrack(0);
    song.selectSection(0);
    check(VerifyContext::usable(ctx.named("followButton"), 40, 14));
    check(ctx.controller.followPlayback());
    // Plays until the playhead is a step or more into bar three, pattern B.
    const auto play_until = [&](int bar) {
        for (int turn = 0; turn < 2000 && !(ctx.transport.bar() == bar && ctx.transport.step() >= 2); ++turn) {
            for (int block = 0; block < 8; ++block) (void)ctx.pump();
            VerifyContext::settle(5);
        }
        lay_out();
        return ctx.transport.bar() == bar;
    };
    const auto drawn = [&ctx] {
        const int step = static_cast<int>(std::floor(ctx.controller.patternPlayStep()));
        auto* grid = ctx.named(QStringLiteral("step%1").arg(step));
        auto* row = ctx.named(QStringLiteral("trackerRow%1").arg(step));
        auto* roll = ctx.named(QStringLiteral("rollPlayhead"));
        return step >= 0 && grid != nullptr && grid->property("playhead").toBool() &&
               row != nullptr && row->property("playhead").toBool() && roll != nullptr &&
               roll->isVisible();
    };
    ctx.controller.seekToBar(0);
    ctx.controller.togglePlayback();
    check(play_until(0) && song.currentSection() == 0 && drawn());
    check(song.sections().size() == 2 && song.patterns().size() == 2);
    check(play_until(2));
    if (song.currentSection() != 1)
        std::cerr << "REGRESSION: the song plays pattern B and the editors still show pattern "
                  << song.currentSection() << '\n';
    check(song.currentSection() == 1 && drawn());

    // FOLLOW off: the editors stay on what is open while the song moves on.
    if (auto* follow = ctx.named("followButton"))
        ctx.click_at(follow, {follow->width() / 2, follow->height() / 2}, Qt::LeftButton);
    check(!ctx.controller.followPlayback());
    ctx.controller.seekToBar(0);
    check(play_until(0) && song.currentSection() == 1 && ctx.controller.patternPlayStep() < 0);
    check(ctx.named("rollPlayhead") != nullptr && !ctx.named("rollPlayhead")->isVisible());
    // And on again, they go straight to where the song is.
    if (auto* follow = ctx.named("followButton"))
        ctx.click_at(follow, {follow->width() / 2, follow->height() / 2}, Qt::LeftButton);
    lay_out();
    check(ctx.controller.followPlayback() && song.currentSection() == 0 && drawn());
    check(ctx.save_screenshot());
    ctx.controller.togglePlayback();
    std::ostringstream details;
    details << " | " << tracks << " tracks x " << bars << " bars | memory " << before << " -> " << after
            << " MiB over five mixer moves";
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("long_song", run_long_song);

}  // namespace
}  // namespace blokkily::verify
