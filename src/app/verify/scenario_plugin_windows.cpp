// features/plugin_windows.feature, run on its own by the bdd_plugin_windows
// gate (`--scenario plugin_windows`), offscreen. A plugin's own window opened
// from the rendered E button and EDITOR bar; a knob turned in it heard in the
// song and taken back by undo; the window surviving a rebuild, closing with
// its instrument, and not coming back from a saved project; a VST3 editor
// refused because offscreen has no X11 display.

#include "verify/harness.hpp"

#include "fixtures/test_clap_gui.hpp"

#include "blokkily/audio/mixer.hpp"

#include <QCoreApplication>
#include <QWindow>

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>

namespace blokkily::verify {
namespace {

namespace gui = blokkily::test_clap_gui;

// A hook of the CLAP fixture, from the module the application itself loaded
// (RTLD_NOLOAD refuses to load a second copy).
template <typename Function>
Function fixture_hook(const QString& path, const char* name) {
    void* module = dlopen(path.toStdString().c_str(), RTLD_NOW | RTLD_NOLOAD);
    if (module == nullptr) return nullptr;
    auto* function = reinterpret_cast<Function>(dlsym(module, name));
    dlclose(module);
    return function;
}

void run_plugin_windows(VerifyContext& ctx) {
    auto& song = ctx.song;
    auto& controller = ctx.controller;
    const QString clap = ctx.parser.value("clap-fixture");
    const QString vst3 = ctx.parser.value("vst3-fixture");
    const auto check = [&ctx](bool ok) { ctx.check(ok); };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto lay_out = [&ctx] {
        (void)ctx.window->grabWindow();
        QCoreApplication::processEvents();
    };
    const auto report = [&clap] {
        std::array<long, gui::report_size> values{};
        values.fill(-1);
        if (auto read = fixture_hook<void (*)(long*)>(clap, "blokkily_test_gui_report"))
            read(values.data());
        return values;
    };
    const auto instances = [&clap] {
        std::array<long, 2> counts{-1, -1};
        if (auto read = fixture_hook<void (*)(long*, long*)>(clap, "blokkily_test_instance_counts"))
            read(&counts[0], &counts[1]);
        return counts;
    };
    const auto turn = [&clap](double value) {
        if (auto knob = fixture_hook<void (*)(std::uint32_t, double)>(clap, "blokkily_test_gui_turn"))
            knob(0, value);
    };
    // The level track 0's CLAP fixture plays at, heard on the bus through the
    // production callback: one key, through the strip.
    const auto level_heard = [&](int track) {
        song.selectTrack(track);
        (void)ctx.pump();
        if (!controller.auditionKey(60, true)) return -1.0F;
        const float peak = ctx.pump();
        controller.releaseAudition();
        (void)ctx.pump();
        const auto& mix = song.song().tracks.at(static_cast<std::size_t>(track)).mix;
        const auto gain = blokkily::strip_gain(mix, song.song().any_solo());
        return peak / std::max(gain.left, gain.right);
    };
    const auto near = [](float heard, float wanted) { return std::abs(heard - wanted) < 1e-3F; };
    const auto label = [&ctx](const char* name) {
        auto* item = ctx.named(QString::fromLatin1(name));
        return item != nullptr ? item->property("text").toString() : QString();
    };
    auto& windows = controller.pluginWindows();
    const auto track0 = blokkily::track_instrument(0);

    // The CLAP fixture on track 0 and the VST3 fixture on track 1, behind the
    // production adapters.
    check(controller.verifyClap(clap));
    check(controller.verifyVst3(vst3));
    // The fixture's report lives in its module; a reference held here keeps
    // it loaded (and its counts running) across a project load, which
    // destroys every instance and would otherwise unload it.
    void* keep_fixture = dlopen(clap.toStdString().c_str(), RTLD_NOW | RTLD_NOLOAD);
    check(keep_fixture != nullptr);
    check(controller.engine() != nullptr && song.song().tracks.at(0).instrument.format == "CLAP" &&
          song.song().tracks.at(1).instrument.format == "VST3");
    song.selectTrack(0);
    check(ctx.window->setProperty("view", "ALL"));
    lay_out();
    VerifyContext::settle(50);
    check(near(level_heard(0), 0.25F));
    reached("plugin windows: the CLAP fixture plays track 0 at 0.25");

    // Step 1-2: E on the strip opens a 320x200 editor embedded in a window
    // made for it: set_parent is given that window's id.
    auto* e0 = ctx.named("editor0");
    auto* e1 = ctx.named("editor1");
    check(VerifyContext::usable(e0, 18, 18) && VerifyContext::usable(e1, 18, 18));
    check(VerifyContext::usable(ctx.named("editorBar"), 150, 28) &&
          VerifyContext::usable(ctx.named("editorButton"), 40, 20));
    // The strip still has room for its own name beside the new button.
    check(VerifyContext::usable(ctx.named("mixerStrip0"), 150, 100));
    const auto before_open = report();
    if (e0 != nullptr) ctx.click_at(e0, {e0->width() / 2, e0->height() / 2}, Qt::LeftButton);
    lay_out();
    const auto opened = report();
    QWindow* window = windows.window(track0);
    check(windows.isOpen(track0) && window != nullptr &&
          windows.placement(track0) == PluginWindows::Placement::embedded);
    check(opened[gui::creates] == before_open[gui::creates] + 1 && opened[gui::open_editors] == 1 &&
          opened[gui::last_floating] == 0);
    check(window != nullptr && opened[gui::last_parent] == static_cast<long>(window->winId()));
    const qreal ratio = window != nullptr ? window->devicePixelRatio() : 1.0;
    check(window != nullptr && std::lround(window->width() * ratio) == gui::editor_width &&
          std::lround(window->height() * ratio) == gui::editor_height);
    check(e0 != nullptr && e0->property("on").toBool() && controller.editorOpen(0));
    if (window == nullptr)
        std::cerr << "plugin windows: no window; status " << controller.editorStatus().toStdString()
                  << '\n';
    reached("plugin windows: E opens a 320x200 editor parented to its own window");

    // Step 3: a knob turned in the editor is heard, and the readout says so.
    turn(0.6);
    (void)ctx.pump();   // the audio thread flushes the turn into the edit ring
    VerifyContext::settle(80);   // the editor timer drains it into the song
    lay_out();
    check(controller.editorReadout() == "LEVEL 0.60" && label("editorReadout") == "LEVEL 0.60");
    check(song.canUndo() && !song.canRedo());
    check(near(level_heard(0), 0.6F));
    reached("plugin windows: LEVEL 0.60 turned in the editor is heard");

    // Step 4: one undo takes the whole gesture back, redo puts it again.
    const auto* engine_before = controller.engine();
    const int rebuilds = controller.rebuildCount();
    auto* undo = ctx.named("undoButton");
    check(VerifyContext::usable(undo, 30, 20));
    if (undo != nullptr) ctx.click_at(undo, {undo->width() / 2, undo->height() / 2}, Qt::LeftButton);
    VerifyContext::settle(20);
    const float undone = level_heard(0);
    if (!song.canRedo() || !near(undone, 0.25F) || controller.engine() != engine_before ||
        controller.rebuildCount() != rebuilds)
        std::cerr << "plugin windows: after undo canRedo=" << song.canRedo() << " level=" << undone
                  << " same engine=" << (controller.engine() == engine_before)
                  << " rebuilds " << rebuilds << "->" << controller.rebuildCount() << '\n';
    check(song.canRedo());
    check(near(undone, 0.25F));
    check(controller.engine() == engine_before && controller.rebuildCount() == rebuilds);
    auto* redo = ctx.named("redoButton");
    if (redo != nullptr) ctx.click_at(redo, {redo->width() / 2, redo->height() / 2}, Qt::LeftButton);
    VerifyContext::settle(20);
    check(near(level_heard(0), 0.6F));
    check(windows.isOpen(track0) && windows.window(track0) == window);
    reached("plugin windows: undo takes the turn back to 0.25 and redo returns it");

    // Step 5: adding a track rebuilds the graph; the editor's instance is
    // adopted, so the same window stays open and nothing is destroyed.
    const auto created_before = instances();
    const auto gui_before = report();
    const int tracks_before = song.trackCount();
    controller.addTrack();
    VerifyContext::settle(20);
    check(controller.rebuildCount() == rebuilds + 1 && song.trackCount() == tracks_before + 1);
    check(windows.isOpen(track0) && windows.window(track0) == window);
    check(instances() == created_before);
    check(report()[gui::destroys] == gui_before[gui::destroys] &&
          report()[gui::creates] == gui_before[gui::creates]);
    check(near(level_heard(0), 0.6F));
    reached("plugin windows: adding a track keeps the same window, nothing destroyed");

    // Step 6: an instrument swap on a track with an open editor closes it.
    // The new track is given the CLAP fixture too, its editor opened, then a
    // SoundFont swapped in over it.
    {
        const int added = song.trackCount() - 1;
        const auto where = blokkily::track_instrument(static_cast<std::uint32_t>(added));
        blokkily::InstrumentSlot slot{"CLAP", clap.toStdString(), "dev.blokkily.test", {}};
        song.setInstrument(added, slot);
        VerifyContext::settle(20);
        lay_out();
        auto* e_added = ctx.named(QString("editor%1").arg(added));
        check(VerifyContext::usable(e_added, 18, 18));
        if (e_added != nullptr)
            ctx.click_at(e_added, {e_added->width() / 2, e_added->height() / 2}, Qt::LeftButton);
        check(windows.isOpen(where) && windows.count() == 2);
        const auto swap_before = report();
        blokkily::InstrumentSlot soundfont{"SoundFont",
                                           ctx.parser.value("soundfont-fixture").toStdString(), "", {}};
        song.setInstrument(added, soundfont);
        VerifyContext::settle(20);
        lay_out();
        check(!windows.isOpen(where) && windows.count() == 1);
        check(report()[gui::destroys] == swap_before[gui::destroys] + 1 &&
              report()[gui::open_editors] == 1);
        check(windows.isOpen(track0) && windows.window(track0) == window);
        check(e_added != nullptr && !e_added->property("on").toBool());
        // A SoundFont has no editor of its own.
        check(!controller.openEditor(added) &&
              controller.editorStatus() == "This instrument has no editor");
        reached("plugin windows: an instrument swap closes its editor, the other stays");
    }

    // Steps 7-8: save, then load: the session comes back with its level and
    // no window opens by itself; the one that was open closed with its
    // instance.
    {
        const QString path = ctx.parser.value("project");
        check(!path.isEmpty() && controller.saveProject(path));
        const auto load_before = report();
        check(controller.loadProject(path));
        VerifyContext::settle(20);
        lay_out();
        const auto loaded = report();
        const float level = level_heard(0);
        if (windows.count() != 0 || loaded[gui::creates] != load_before[gui::creates] ||
            loaded[gui::open_editors] != 0 || !near(level, 0.6F))
            std::cerr << "plugin windows: after load windows=" << windows.count()
                      << " creates " << load_before[gui::creates] << "->" << loaded[gui::creates]
                      << " open=" << loaded[gui::open_editors] << " level=" << level << '\n';
        check(windows.count() == 0 && controller.openEditors().isEmpty());
        check(loaded[gui::creates] == load_before[gui::creates] && loaded[gui::open_editors] == 0);
        check(near(level, 0.6F));
        reached("plugin windows: save and load opens no windows and keeps LEVEL 0.60");
    }

    // Step 9: EDITOR in the instrument panel opens the selected track's
    // editor again.
    song.selectTrack(0);
    lay_out();
    auto* editor_button = ctx.named("editorButton");
    check(VerifyContext::usable(editor_button, 40, 20));
    if (editor_button != nullptr)
        ctx.click_at(editor_button, {editor_button->width() / 2, editor_button->height() / 2},
                     Qt::LeftButton);
    lay_out();
    check(windows.isOpen(track0) && report()[gui::open_editors] == 1);
    check(editor_button != nullptr && editor_button->property("on").toBool());
    reached("plugin windows: EDITOR in the instrument panel opens the selected track's editor");

    // Step 10: offscreen there is no X11 display, so the VST3 editor, which
    // JUCE would put on X, is refused with a clear message, and nothing else
    // is disturbed.
    {
        auto* e_vst3 = ctx.named("editor1");
        check(e_vst3 != nullptr && e_vst3->isEnabled());
        if (e_vst3 != nullptr)
            ctx.click_at(e_vst3, {e_vst3->width() / 2, e_vst3->height() / 2}, Qt::LeftButton);
        lay_out();
        check(!windows.isOpen(blokkily::track_instrument(1)));
        check(controller.editorStatus() == "Plugin window needs an X11 display");
        check(label("editorStatus") == "Plugin window needs an X11 display");
        check(windows.isOpen(track0) && near(level_heard(1), 0.25F));
        reached("plugin windows: a VST3 editor is refused offscreen with a clear message");
    }

    song.selectTrack(0);
    lay_out();
    reached("plugin windows: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    std::ostringstream details;
    const auto final_report = report();
    details << " | editors=" << windows.count() << " | gui_creates=" << final_report[gui::creates]
            << " | gui_destroys=" << final_report[gui::destroys]
            << " | readout=" << controller.editorReadout().toStdString()
            << " | status=" << controller.editorStatus().toStdString();
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("plugin_windows", run_plugin_windows);

}  // namespace
}  // namespace blokkily::verify
