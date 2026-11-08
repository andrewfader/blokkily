// features/track_list.feature, run by bdd_track_list: the arrangement's track
// list sized by the pointer at its edge, and a track muted by M after a click on
// its name and by the right button's menu. Everything is pointer and key events
// at the real window; a mute is heard through the production render callback.
#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QMetaObject>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

blokkily::Song two_tracks() {
    // A note at the top of every sixteenth, so a block always holds a sounding one.
    blokkily::Pattern pattern(1920, 480);
    for (int step = 0; step < 16; ++step) {
        blokkily::Trigger trigger;
        trigger.start = step * 120;
        trigger.duration = 110;
        trigger.musical_data = blokkily::Note{60, 1.0F, 0.0F};
        (void)pattern.add(trigger);
    }
    blokkily::Song song;
    song.patterns = {{"LOOP", std::move(pattern)}};
    song.tracks = {blokkily::Track{}, blokkily::Track{}};
    song.tracks[0].name = "A TRACK WITH QUITE A LONG NAME";
    song.tracks[1].name = "QUIET";
    song.clips = {{0, 0, 0, 8}};
    return song;
}

void run_track_list(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    auto* const window = ctx.window;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "track list failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)window->grabWindow(); };
    const auto centre = [](QQuickItem* item) {
        return item ? QPointF(item->width() / 2, item->height() / 2) : QPointF();
    };
    const auto click = [&](QQuickItem* item, Qt::MouseButton button = Qt::LeftButton) {
        check(item != nullptr);
        if (item != nullptr) ctx.click_at(item, centre(item), button);
        layout();
    };
    // The points are fixed in the window when the press lands: the edge moves
    // with what it sizes.
    const auto drag = [&](QQuickItem* item, double dx) {
        if (item == nullptr) return;
        auto* scene = window->contentItem();
        const QPointF from = item->mapToScene(centre(item));
        ctx.mouse_at(scene, from, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        for (int step = 1; step <= 10; ++step) {
            ctx.mouse_at(scene, from + QPointF(dx * step / 10.0, 0), QEvent::MouseMove, Qt::NoButton,
                         Qt::LeftButton);
            layout();
        }
        ctx.mouse_at(scene, from + QPointF(dx, 0), QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        layout();
    };
    const auto muted = [&](int track) { return song.song().tracks.at(static_cast<std::size_t>(track)).mix.mute; };
    const auto header = [&](int track) { return ctx.named(QStringLiteral("trackHeader%1").arg(track)); };
    const auto scene_x = [](QQuickItem* item) { return item ? item->mapToScene(QPointF(0, 0)).x() : -1e6; };
    const auto notes_on_part = [&] {
        int count = 0;
        for (int step = 0; step < pattern.stepCount(); ++step) count += pattern.hasStep(step) ? 1 : 0;
        return count;
    };
    // The loudest sample over a few blocks of the production callback, after
    // the blocks a change takes to reach it.
    const auto heard = [&] {
        for (int block = 0; block < 4; ++block) (void)ctx.pump();
        float loudest = 0.0F;
        for (int block = 0; block < 8; ++block) loudest = std::max(loudest, ctx.pump());
        return loudest;
    };

    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    check(c.engine() != nullptr && song.song().tracks.at(0).instrument.format == "CLAP");
    {
        const auto synth = song.song().tracks.at(0).instrument;
        auto session = two_tracks();
        for (auto& track : session.tracks) track.instrument = synth;
        song.replace(std::move(session));
        c.flushRecompile();
    }
    c.setTempo(120.0);
    song.selectTrack(1);
    check(window->setProperty("view", "ALL"));
    c.seekToBar(0);
    layout();

    // ------------------------------------------------- the edge sizes the list
    auto* const sizer = ctx.named("trackListSizer");
    auto* const view = ctx.named("arrangementView");
    auto* const scroll = ctx.named("arrangeScroll");
    check(VerifyContext::usable(sizer, 4, 100) && view != nullptr && scroll != nullptr);
    check(VerifyContext::usable(header(0), 60, 20));
    const double shipped = header(0) ? header(0)->width() : 0;
    check(shipped == 64);
    const auto bar = [&](int number) { return ctx.named(QStringLiteral("clip0-%1").arg(number)); };
    const double bar_shipped = bar(1) ? bar(1)->width() : 0;

    drag(sizer, 100);
    std::cerr << "list widened to " << (header(0) ? header(0)->width() : -1) << '\n';
    check(header(0) && header(0)->width() == shipped + 100);
    check(header(1) && header(1)->width() == shipped + 100);
    check(c.uiSettings().value("ui/trackListWidth").toInt() == 164);
    // The whole song still fits, in what the list has left it.
    check(bar(1) && bar(1)->width() < bar_shipped && VerifyContext::usable(bar(1), 40, 20));
    check(scroll && std::abs(scroll->property("contentWidth").toDouble() - scroll->width()) < 2);
    // Everything over the bars moved with them.
    auto* const ruler = ctx.named("rulerBar1");
    check(ruler && bar(1) && std::abs(scene_x(ruler) - scene_x(bar(1))) < 1);
    auto* const clip_lane = ctx.named("audioClipLane");
    check(clip_lane && std::abs(clip_lane->property("cellsX").toDouble() - (header(0)->width() + 2)) < 1);
    check(clip_lane && std::abs(scene_x(clip_lane) + clip_lane->property("cellsX").toDouble()
                                - scene_x(ctx.named("clip0-0"))) < 1);
    // A press right beside the edge is the first bar's, not the edge's: it
    // opens the clip there, which puts the playhead on that bar.
    {
        auto* first = ctx.named("clip0-0");
        c.seekToBar(3);
        layout();
        check(first != nullptr && ctx.transport.bar() == 3);
        if (first != nullptr) ctx.click_at(first, {1.5, first->height() / 2}, Qt::LeftButton);
        VerifyContext::settle(20);
        layout();
        check(ctx.transport.bar() == 0 && c.uiSettings().value("ui/trackListWidth").toInt() == 164);
    }
    check(ctx.save_screenshot());
    ctx.reached("track list: its edge widens it, and the bars and ruler follow");

    // Past the most it can take: half the view, and the bars keep the rest.
    drag(sizer, 2000);
    std::cerr << "list at most " << (header(0) ? header(0)->width() : -1) << " of " << scroll->width() << '\n';
    check(header(0) && header(0)->width() <= scroll->width() / 2 + 1 && header(0)->width() <= 320);
    drag(sizer, -2000);
    check(header(0) && header(0)->width() >= 44);
    {
        const QPointF middle = centre(sizer);
        ctx.mouse_at(sizer, middle, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        ctx.mouse_at(sizer, middle, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        ctx.mouse_at(sizer, middle, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        ctx.mouse_at(sizer, middle, QEvent::MouseButtonDblClick, Qt::LeftButton, Qt::LeftButton);
        ctx.mouse_at(sizer, middle, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        layout();
    }
    check(header(0) && header(0)->width() == shipped && !c.uiSettings().contains("ui/trackListWidth"));
    ctx.reached("track list: it stops at half the view, and a double-click gives back its width");

    // --------------------------------------------------------- M, heard
    c.flushRecompile();
    c.seekToBar(0);
    c.togglePlayback();
    check(ctx.transport.playing());
    const float loud = heard();
    std::cerr << "heard unmuted " << loud << '\n';
    check(loud > 0.05F);

    click(header(0));
    check(song.selectedTrack() == 0 && !muted(0));
    check(view && view->property("headerHasKeys").toBool());
    const int notes_before = notes_on_part();
    const int rebuilds = c.rebuildCount();
    ctx.type_key(Qt::Key_M, QStringLiteral("m"));
    layout();
    check(muted(0));
    check(notes_on_part() == notes_before);
    auto* const flags = ctx.named("trackFlags0");
    check(flags && flags->width() > 0);
    const float silent = heard();
    std::cerr << "heard muted " << silent << '\n';
    check(silent < 1e-3F);
    if (ctx.parser.isSet("screenshot")) {
        const QString path = ctx.parser.value("screenshot");
        check(ctx.save_screenshot_to(path.left(path.size() - 4) + QStringLiteral("-muted.png")));
    }

    ctx.type_key(Qt::Key_M, QStringLiteral("m"));
    layout();
    check(!muted(0));
    const float back = heard();
    std::cerr << "heard unmuted again " << back << '\n';
    check(back > 0.05F);
    check(c.rebuildCount() == rebuilds);
    check(song.undo() && muted(0));
    check(song.undo() && !muted(0));
    layout();
    c.togglePlayback();
    (void)ctx.pump();
    ctx.reached("track list: M after a click on a track's name mutes it, heard, and M brings it back");

    // ------------------------------------------- M is a note everywhere else
    song.selectTrack(1);
    layout();
    check(notes_on_part() == 0);
    pattern.selectStep(0);
    // Straight after a click on the name, a phrase: Z ends it, so M is a note.
    click(header(1));
    check(view && view->property("headerHasKeys").toBool());
    ctx.type_key(Qt::Key_Z, QStringLiteral("z"));
    ctx.type_key(Qt::Key_M, QStringLiteral("m"));
    layout();
    check(!muted(1) && pattern.hasStep(0) && pattern.hasStep(1));
    check(view && !view->property("headerHasKeys").toBool());
    // A tracker row moved to.
    click(header(1));
    check(view && view->property("headerHasKeys").toBool());
    ctx.type_key(Qt::Key_Down, QString());
    layout();
    check(view && !view->property("headerHasKeys").toBool());
    const int step = pattern.selectedStep();
    ctx.type_key(Qt::Key_M, QStringLiteral("m"));
    layout();
    check(!muted(1) && pattern.hasStep(step));
    // A press anywhere else: here on the ruler, which only moves the playhead.
    click(header(1));
    check(view && view->property("headerHasKeys").toBool());
    if (auto* elsewhere = ctx.named("rulerBar0")) ctx.click_at(elsewhere, centre(elsewhere), Qt::LeftButton);
    layout();
    check(view && !view->property("headerHasKeys").toBool());
    const int next = pattern.selectedStep();
    ctx.type_key(Qt::Key_M, QStringLiteral("m"));
    layout();
    check(!muted(1) && pattern.hasStep(next));
    ctx.reached("track list: M is a note after a note typed, a step moved or a press elsewhere");

    // ------------------------------------------------- the right button
    // The second track has the notes just typed: out of the way, so what is
    // heard is the first track alone.
    song.toggleMute(1);
    song.selectTrack(1);
    layout();
    auto* const menu = window->findChild<QObject*>("trackMenu");
    auto* const mute_item = window->findChild<QQuickItem*>("trackMenuMute");
    auto* const rename_item = window->findChild<QQuickItem*>("trackMenuRename");
    check(menu && mute_item && rename_item);
    c.flushRecompile();
    c.seekToBar(0);
    c.togglePlayback();
    check(heard() > 0.05F);
    click(header(0), Qt::RightButton);
    VerifyContext::settle(50);
    layout();
    check(menu && menu->property("opened").toBool() && menu->property("target").toInt() == 0);
    check(mute_item && rename_item && mute_item->y() < rename_item->y());
    check(VerifyContext::usable(mute_item, 60, 16));
    if (ctx.parser.isSet("screenshot")) {
        const QString path = ctx.parser.value("screenshot");
        check(ctx.save_screenshot_to(path.left(path.size() - 4) + QStringLiteral("-menu.png")));
    }
    click(mute_item);
    VerifyContext::settle(50);
    layout();
    check(muted(0) && menu && !menu->property("opened").toBool());
    check(mute_item && mute_item->property("checked").toBool());
    const float silenced = heard();
    std::cerr << "heard after the menu's mute " << silenced << '\n';
    check(silenced < 1e-3F);
    // And the menu brings it back.
    click(header(0), Qt::RightButton);
    VerifyContext::settle(50);
    layout();
    click(mute_item);
    VerifyContext::settle(50);
    layout();
    check(!muted(0) && mute_item && !mute_item->property("checked").toBool());
    check(heard() > 0.05F);
    c.togglePlayback();
    (void)ctx.pump();
    ctx.reached("track list: the right button's menu has Mute first, and it is heard");

    ctx.finish("track list sized by its edge, muted by M and by its menu");
}

[[maybe_unused]] const bool registered = register_scenario("track_list", run_track_list);

} // namespace
} // namespace blokkily::verify
