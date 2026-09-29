// features/scene_launcher.feature, run by the bdd_launcher gate
// (`--scenario launcher`). The scene launcher in the real application, driven
// through the rendered interface and heard through the production render
// callback: the LAUNCH view is picked from the view switcher, scenes are added
// and cells filled by clicking the grid, a follow action and the scene
// quantization are chosen from the rendered pickers, arrangement recording is
// switched on, a scene is launched from its chip and heard (its follow action
// too), tracks are stopped from their chips, the takes are printed into the
// arrangement as one step of history each, and the song is saved, loaded and
// exported with them.

#include "verify/harness.hpp"

#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <source_location>
#include <sstream>
#include <tuple>
#include <vector>

namespace blokkily::verify {
namespace {

Pattern held_note(float velocity) {
    Pattern pattern(1920, 480);
    Trigger note;
    note.start = 0;
    note.duration = 1900;
    note.musical_data = Note{60, velocity, 0.0F};
    // The fixture's velocity mode: it sounds 0.25 x the velocity held, so
    // which cell plays is read from the level.
    note.locks = {{"velocity-mode", 2, 1.0, ParameterLock::Kind::automation}};
    (void)pattern.add(note);
    return pattern;
}

void run_launcher(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& controller = ctx.controller;
    const auto& parser = ctx.parser;
    const auto check = [&ctx](bool ok, std::source_location at = std::source_location::current()) {
        if (!ok) std::cerr << "launcher: check failed at line " << at.line() << '\n';
        ctx.check(ok);
    };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    const auto reveal = [&](QQuickItem* item) {
        if (item == nullptr) return;
        auto* scroll = ctx.named("editorScroll");
        bool inside = false;
        for (auto* parent = item->parentItem(); parent; parent = parent->parentItem())
            if (parent == scroll) inside = true;
        if (!inside) return;
        const auto y = item->mapToItem(scroll, QPointF(0, 0)).y();
        const double current = scroll->property("contentY").toDouble();
        if (y < 0 || y + item->height() > scroll->height())
            scroll->setProperty("contentY", std::max(0.0, current + y - scroll->height() / 3.0));
        lay_out();
    };
    const auto click = [&](QQuickItem* item, Qt::MouseButton button = Qt::LeftButton) {
        if (item == nullptr) return false;
        reveal(item);
        ctx.click_at(item, {item->width() / 2.0, item->height() / 2.0}, button);
        lay_out();
        return true;
    };
    const auto click_named = [&](const QString& name, Qt::MouseButton button = Qt::LeftButton) {
        auto* item = ctx.named(name);
        check(VerifyContext::usable(item, 16, 16));
        return click(item, button);
    };
    const auto pick = [&](const QString& picker, int row) {
        check(click_named(picker));
        auto* owner = ctx.named(picker);
        auto* menu = owner == nullptr ? nullptr : owner->findChild<QObject*>(picker + "Menu");
        check(menu != nullptr && menu->property("opened").toBool());
        QQuickItem* entry = nullptr;
        if (menu != nullptr)
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, entry),
                                      Q_ARG(int, row));
        check(entry != nullptr);
        if (entry != nullptr)
            ctx.click_at(entry, {entry->width() / 2, entry->height() / 2}, Qt::LeftButton);
        lay_out();
    };
    const auto gain = [&](std::size_t track) {
        const auto g = strip_gain(song.song().tracks.at(track).mix, song.song().any_solo());
        return static_cast<double>(g.left);
    };
    const auto level = [&](double velocity, std::size_t track) {
        return 0.25 * velocity * gain(track);
    };
    const auto near = [](double actual, double expected, double tolerance = 2e-4) {
        return std::abs(actual - expected) <= tolerance;
    };
    // Pumps the production callback until the song has played to `sample`
    // (counted from where the transport started); the loudest sample of the
    // last block.
    std::uint64_t played = 0;
    const auto play_to = [&](std::uint64_t sample) {
        float last = 0.0F;
        while (played < sample) {
            last = ctx.pump();
            if (last < 0.0F) return last;
            // A pump is 1024 interleaved samples: 512 frames.
            played += 512;
        }
        return last;
    };
    const auto cell_row = [&](int scene, int track) {
        const auto scenes = song.launcherScenes();
        if (scene >= scenes.size()) return QVariantMap{};
        const auto cells = scenes.at(scene).toMap().value("cells").toList();
        return track < cells.size() ? cells.at(track).toMap() : QVariantMap{};
    };

    // --- The session: the CLAP synth on BASS and LEAD, three patterns, no
    // clips: the arrangement is empty until the launcher prints into it. ----
    check(controller.verifyClap(parser.value("clap-fixture")));
    controller.scanPluginPaths({parser.value("clap-fixture").toStdString()},
                               {parser.value("vst3-fixture").toStdString()},
                               {parser.value("soundfont-fixture").toStdString()});
    {
        auto configured = song.song();
        const auto synth = configured.tracks.at(0).instrument;
        configured.tracks.assign(2, Track{});
        configured.tracks[0].name = "BASS";
        configured.tracks[0].instrument = synth;
        configured.tracks[1].name = "LEAD";
        configured.tracks[1].instrument = synth;
        configured.patterns = {{"GROOVE", held_note(0.4F)}, {"HOOK", held_note(0.8F)},
                               {"FILL", held_note(0.6F)}};
        configured.clips.clear();
        configured.launcher = SceneMatrix{};
        configured.modulators.clear();
        song.replace(std::move(configured));
        controller.flushRecompile();
    }
    check(song.song().tracks.size() == 2 && controller.engine() != nullptr &&
          controller.engine()->has_instrument(0) && controller.engine()->has_instrument(1));
    const int rebuilds = controller.rebuildCount();
    reached("launcher: two CLAP tracks, three patterns");

    // --- 1. The LAUNCH view, from the view switcher. ----------------------
    check(window->property("view").toString() == "ALL");
    check(click_named("viewLAUNCH"));
    check(window->property("view").toString() == "LAUNCH");
    auto* view = ctx.named("launcherView");
    check(VerifyContext::usable(view, 700, 300));
    check(ctx.named("launcherEmpty") != nullptr && ctx.named("launcherEmpty")->isVisible());
    // The step editors give the launcher their place.
    auto* grid_editor = ctx.named("stepGrid");
    check(grid_editor == nullptr || !grid_editor->isVisible());
    reached("launcher: the LAUNCH view shows the grid in the editors' place");

    // --- 2. Two scenes, cells filled by clicking. -------------------------
    check(click_named("addScene"));
    check(click_named("addScene"));
    check(song.song().launcher.scenes.size() == 2 &&
          song.song().launcher.scenes[0].name == "SCENE 1");
    check(VerifyContext::usable(ctx.named("launcherGrid"), 600, 150));
    for (const auto& [scene, track, pattern] :
         std::vector<std::tuple<int, int, int>>{{0, 0, 0}, {0, 1, 1}, {1, 0, 2}}) {
        song.selectPattern(pattern);
        auto* cell = ctx.named(QString("cell%1_%2").arg(scene).arg(track));
        check(VerifyContext::usable(cell, 100, 30));
        check(click(cell));
        const auto& slot = song.song().launcher.slot(static_cast<std::size_t>(scene),
                                                     static_cast<std::size_t>(track));
        check(slot.has_value() && slot->pattern == static_cast<std::size_t>(pattern));
    }
    check(cell_row(0, 0).value("name").toString() == "GROOVE" &&
          cell_row(1, 0).value("name").toString() == "FILL" &&
          !cell_row(1, 1).value("filled").toBool());
    reached("launcher: clicking empty cells puts the open pattern in them");

    // --- 3. A follow action and the quantizations, from the pickers. ------
    check(click_named("cell0_0", Qt::RightButton));
    check(VerifyContext::usable(ctx.named("cellInspector"), 400, 20));
    // NEXT is the third follow action.
    pick("cellFollow", 2);
    {
        const auto& slot = song.song().launcher.slot(0, 0);
        check(slot && slot->follow_action == FollowAction::next && slot->repeats == 1);
    }
    // Scenes and stops wait for a beat.
    pick("launcherQuantization", 2);
    check(song.song().launcher.quantization == LaunchQuantization::beat);
    check(click_named("launcherRecord"));
    check(controller.launcherRecording() && controller.engine()->launcher_recording());
    check(controller.rebuildCount() == rebuilds);
    reached("launcher: follow action, quantization and arrangement recording set");

    // --- 4. Scene 1 launched from its chip, heard. -------------------------
    check(!ctx.transport.playing());
    check(click_named("launchScene0"));
    check(ctx.transport.playing());
    const double rate = controller.engine()->sample_rate();
    const auto bar = static_cast<std::uint64_t>(std::llround(rate * 2.0)); // 4/4 at 120 bpm
    const float first = play_to(2048);
    check(near(first, level(0.4, 0) + level(0.8, 1)));
    controller.pollLauncher();
    lay_out();
    check(ctx.named("cell0_0") != nullptr && ctx.named("cell0_0")->property("playing").toBool() &&
          ctx.named("cell0_1")->property("playing").toBool());
    std::cerr << "launcher: scene 1 heard at " << first << '\n';
    reached("launcher: the scene chip launches both cells, heard from the first block");

    // Its follow action: after one loop BASS plays FILL; LEAD loops on.
    const float followed = play_to(bar + 8192);
    std::cerr << "launcher: after one loop " << followed << '\n';
    check(near(followed, level(0.6, 0) + level(0.8, 1)));
    controller.pollLauncher();
    lay_out();
    check(ctx.named("cell1_0")->property("playing").toBool() &&
          !ctx.named("cell0_0")->property("playing").toBool());
    reached("launcher: the follow action moves BASS to the next scene");

    // --- 5. Stopped from the rendered stop chips, on the beat. ------------
    check(click_named("stopTrack1"));
    const float bass_alone = play_to(bar + 8192 + 2 * static_cast<std::uint64_t>(rate / 2.0));
    check(near(bass_alone, level(0.6, 0)));
    check(click_named("stopAllLaunched"));
    const float silence = play_to(bar + 8192 + 4 * static_cast<std::uint64_t>(rate / 2.0));
    check(silence == 0.0F);
    controller.pollLauncher();
    lay_out();
    check(!controller.launcherState().at(0).toMap().value("playing").toBool() &&
          !controller.launcherState().at(1).toMap().value("playing").toBool());
    reached("launcher: the stop chips silence their tracks on the beat");

    // --- 6. The takes, printed into the arrangement: one undo step each. --
    check(controller.launcherTakesPrinted() == 3);
    {
        const auto& clips = song.song().clips;
        const auto on = [&](std::size_t track, std::size_t pattern, Tick start) {
            return std::any_of(clips.begin(), clips.end(), [&](const Clip& clip) {
                return clip.track == track && clip.pattern == pattern && clip.start == start;
            });
        };
        check(clips.size() == 3 && on(0, 0, 0) && on(0, 2, 1920) && on(1, 1, 0));
        check(song.song().consistent() && song.canUndo());
        // The last take undone and redone, alone.
        check(song.undo() && song.song().clips.size() == 2);
        check(song.redo() && song.song().clips.size() == 3);
    }
    if (ctx.transport.playing()) controller.togglePlayback();
    reached("launcher: every take is printed into the arrangement");

    // --- 7. Saved, loaded, exported. ---------------------------------------
    const auto launcher_before = song.song().launcher;
    const QString project = parser.value("project");
    check(!project.isEmpty() && controller.saveProject(project));
    check(controller.loadProject(project));
    lay_out();
    check(song.song().launcher == launcher_before && song.song().clips.size() == 3);
    const QString exported = parser.value("export");
    check(!exported.isEmpty() && controller.exportAudioFile(exported, "FLOAT32"));
    {
        std::string error;
        const auto wave = read_wave(exported.toStdString(), &error);
        check(wave.has_value() && wave->channels == 2);
        // The export plays the printed takes: bar 0 GROOVE and HOOK, and
        // early in bar 1 FILL and HOOK (HOOK was stopped on bar 1's second
        // beat).
        const auto at = [&](std::uint64_t frame) {
            return wave && frame * 2 < wave->interleaved.size()
                       ? static_cast<double>(wave->interleaved[frame * 2])
                       : -1.0;
        };
        check(near(at(bar / 2), level(0.4, 0) + level(0.8, 1)));
        check(near(at(bar + 4096), level(0.6, 0) + level(0.8, 1)));
        check(near(at(bar + 3 * bar / 8), level(0.6, 0)));
        std::cerr << "launcher: export bar 0 " << at(bar / 2) << ", bar 1 " << at(bar + 4096)
                  << " then " << at(bar + 3 * bar / 8) << '\n';
    }
    reached("launcher: the project keeps the grid and the takes, and exports them");

    // --- 8. Deleting a pattern keeps the grid pointing at the right ones. --
    check(song.deletePattern(0));
    check(song.song().consistent() && !song.song().launcher.slot(0, 0).has_value() &&
          song.song().launcher.slot(0, 1)->pattern == 0 &&
          song.song().launcher.slot(1, 0)->pattern == 1);
    lay_out();
    check(cell_row(0, 1).value("name").toString() == "HOOK" &&
          cell_row(1, 0).value("name").toString() == "FILL");
    check(song.undo() && song.song().launcher.slot(0, 0)->pattern == 0);
    reached("launcher: deleting a pattern re-indexes its cells");

    // --- The picture: scene 1 playing in the grid, the cell inspector open. -
    check(window->setProperty("view", "LAUNCH"));
    check(controller.launchScene(0));
    for (int block = 0; block < 4; ++block) (void)ctx.pump();
    controller.pollLauncher();
    check(click_named("cell0_0", Qt::RightButton));
    VerifyContext::settle(50);
    lay_out();
    check(VerifyContext::usable(ctx.named("launcherView"), 700, 300) &&
          VerifyContext::usable(ctx.named("launcherGrid"), 600, 150) &&
          VerifyContext::usable(ctx.named("cellInspector"), 400, 20) &&
          VerifyContext::usable(ctx.named("cell1_0"), 100, 30) &&
          ctx.named("cell0_0")->property("playing").toBool());
    reached("launcher: screenshot state");
    // Three patterns used to widen the arrangement toolbar past the pane,
    // pushing every panel of the editor column under the mixer: each must
    // fit the pane it is drawn in.
    {
        auto* scroll = ctx.named(QStringLiteral("editorScroll"));
        auto* bar = ctx.named(QStringLiteral("promptBar"));
        bool fits = scroll != nullptr && bar != nullptr;
        if (fits)
            for (auto* panel : bar->parentItem()->childItems())
                if (panel->isVisible() && panel->width() > scroll->width() + 0.5) {
                    std::cerr << "REGRESSION: " << panel->metaObject()->className() << " is "
                              << panel->width() << " px in a " << scroll->width() << " px pane\n";
                    fits = false;
                }
        check(fits);
    }
    ctx.reached("every editor panel fits its pane");
    check(ctx.save_screenshot());
    reached("screenshot");
    std::ostringstream details;
    details << " | rebuilds=" << controller.rebuildCount()
            << " | takes=" << controller.launcherTakesPrinted()
            << " | clips=" << song.song().clips.size() << " | "
            << controller.exportStatus().toStdString();
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("launcher", run_launcher);

}  // namespace
}  // namespace blokkily::verify
