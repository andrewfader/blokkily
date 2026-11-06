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
    // A strip move re-reads only what shows strips: nothing that draws the
    // arrangement (patterns, clips, lanes, sections, track names) is
    // announced, so a fader dragged across a long song stays cheap.
    int arrangement_announced = 0;
    const auto counting = QObject::connect(&song, &SongModel::arrangementChanged,
                                           [&arrangement_announced] { ++arrangement_announced; });
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
    if (arrangement_announced != 0)
        std::cerr << "REGRESSION: five mixer moves announced the arrangement "
                  << arrangement_announced << " times\n";
    check(arrangement_announced == 0);
    // A renamed track is an arrangement change, and every list of names has it.
    song.renameTrack(4, "RENAMED");
    lay_out();
    check(arrangement_announced >= 1 && song.trackNames().value(4) == "RENAMED");
    QObject::disconnect(counting);
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
        // B is eight bars long, so the step grid, the tracker and the roll
        // have to scroll through it.
        for (std::size_t section = 0; section < 2; ++section) {
            Pattern part(section == 0 ? 1920 : 8 * 1920, 480);
            for (Tick step = 0; step < (section == 0 ? 16 : 128); step += 2) {
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

    // ---------------------------------------------------------------------
    ctx.reached("long_song: stopped in B, one press of rewind shows the start in every view");
    check(play_until(2) && song.currentSection() == 1);
    ctx.controller.togglePlayback();
    VerifyContext::settle(5);
    lay_out();
    check(!ctx.transport.playing());
    auto* arrangement = ctx.named("arrangementView");
    auto* arrange_scroll = ctx.named("arrangeScroll");
    auto* tracker_scroll = ctx.named("trackerScroll");
    auto* grid = ctx.named("stepGrid");
    auto* roll = ctx.named("rollArea");
    check(arrangement != nullptr && arrange_scroll != nullptr && tracker_scroll != nullptr &&
          grid != nullptr && roll != nullptr);
    if (arrangement != nullptr) arrangement->setProperty("zoom", 8);
    lay_out();
    const auto scroll_away = [&] {
        if (arrange_scroll != nullptr)
            arrange_scroll->setProperty("contentX", arrange_scroll->property("contentWidth").toDouble()
                                                        - arrange_scroll->width());
        if (tracker_scroll != nullptr)
            tracker_scroll->setProperty("contentY", tracker_scroll->property("contentHeight").toDouble()
                                                        - tracker_scroll->height());
        if (grid != nullptr) grid->setProperty("scrollX", grid->property("maxScroll"));
        if (roll != nullptr) roll->setProperty("scrollX", roll->property("maxScroll"));
        lay_out();
    };
    const auto scrolled = [](QObject* object, const char* property) {
        return object != nullptr && object->property(property).toDouble() > 0.5;
    };
    const auto at_start = [](QObject* object, const char* property) {
        return object != nullptr && std::abs(object->property(property).toDouble()) < 0.5;
    };
    scroll_away();
    check(scrolled(arrange_scroll, "contentX") && scrolled(tracker_scroll, "contentY") &&
          scrolled(grid, "scrollX") && scrolled(roll, "scrollX"));
    // A seek to a bar of B while stopped: the editors scroll to the step it landed on.
    ctx.controller.seekToBar(2);
    lay_out();
    check(song.currentSection() == 1 && ctx.controller.patternPlayStep() == 0.0 && drawn());
    if (!at_start(tracker_scroll, "contentY") || !at_start(grid, "scrollX") || !at_start(roll, "scrollX"))
        std::cerr << "REGRESSION: a seek on a stopped song left the editors scrolled away from the playhead\n";
    check(at_start(tracker_scroll, "contentY") && at_start(grid, "scrollX") && at_start(roll, "scrollX"));
    check(at_start(arrange_scroll, "contentX") || arrange_scroll->property("contentX").toDouble() <
                                                     arrange_scroll->property("contentWidth").toDouble() / 2);
    // One press of the rewind button: pattern A opens on its first step and the
    // arrangement shows bar 1, from wherever the views were.
    scroll_away();
    auto* rewind = ctx.named("rewindButton");
    check(rewind != nullptr);
    if (rewind != nullptr) ctx.click_at(rewind, {rewind->width() / 2, rewind->height() / 2}, Qt::LeftButton);
    lay_out();
    if (song.currentSection() != 0)
        std::cerr << "REGRESSION: rewind on a stopped song left the editors on pattern "
                  << song.currentSection() << '\n';
    check(song.currentSection() == 0 && ctx.transport.tick() == 0.0 && ctx.transport.bar() == 0 &&
          ctx.controller.patternPlayStep() == 0.0 && drawn());
    if (!at_start(arrange_scroll, "contentX"))
        std::cerr << "REGRESSION: rewind on a stopped song left the arrangement scrolled to "
                  << arrange_scroll->property("contentX").toDouble() << '\n';
    check(at_start(arrange_scroll, "contentX"));
    // FOLLOW off: the arrangement still shows the start; the editors stay where they were.
    ctx.controller.seekToBar(2);
    lay_out();
    check(song.currentSection() == 1);
    if (auto* follow = ctx.named("followButton"))
        ctx.click_at(follow, {follow->width() / 2, follow->height() / 2}, Qt::LeftButton);
    check(!ctx.controller.followPlayback());
    scroll_away();
    if (rewind != nullptr) ctx.click_at(rewind, {rewind->width() / 2, rewind->height() / 2}, Qt::LeftButton);
    lay_out();
    check(song.currentSection() == 1 && ctx.transport.tick() == 0.0 && at_start(arrange_scroll, "contentX") &&
          scrolled(tracker_scroll, "contentY") && scrolled(grid, "scrollX"));
    if (auto* follow = ctx.named("followButton"))
        ctx.click_at(follow, {follow->width() / 2, follow->height() / 2}, Qt::LeftButton);
    check(ctx.controller.followPlayback());
    if (arrangement != nullptr) arrangement->setProperty("zoom", 1);
    std::ostringstream details;
    details << " | " << tracks << " tracks x " << bars << " bars | memory " << before << " -> " << after
            << " MiB over five mixer moves";
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("long_song", run_long_song);

}  // namespace
}  // namespace blokkily::verify
