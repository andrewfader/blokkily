// features/sidechain_and_multiout.feature, run by the bdd_plugin_routing gate
// (`--scenario routing`). Plugin sidechains and multi-output instruments in
// the real application, driven through the rendered interface and heard
// through the production render callback: the CLAP effect fixture, inserted
// from the browser, is keyed from a muted, faded track in the rack (a plugin
// with a sidechain input gets the same SIDECHAIN line as the built-in
// compressor); the CLAP synth's aux output is broken out to a mixer channel
// of its own with the strip's + OUT chip, faded and muted there without a
// rebuild, undone, saved, loaded and exported.

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

namespace blokkily::verify {
namespace {

void run_routing(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& controller = ctx.controller;
    const auto& parser = ctx.parser;
    const auto check = [&ctx](bool ok, std::source_location at = std::source_location::current()) {
        if (!ok) std::cerr << "routing: check failed at line " << at.line() << '\n';
        ctx.check(ok);
    };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    const auto reveal = [&](QQuickItem* item) {
        if (item == nullptr) return;
        for (const char* name : {"editorScroll", "mixerScroll"}) {
            auto* scroll = ctx.named(name);
            bool inside = false;
            for (auto* parent = item->parentItem(); parent; parent = parent->parentItem())
                if (parent == scroll) inside = true;
            if (!inside) continue;
            const auto y = item->mapToItem(scroll, QPointF(0, 0)).y();
            const double current = scroll->property("contentY").toDouble();
            if (y < 0 || y + item->height() > scroll->height())
                scroll->setProperty("contentY",
                                    std::max(0.0, current + y - scroll->height() / 3.0));
            lay_out();
        }
    };
    const auto click = [&](QQuickItem* item) {
        if (item == nullptr) return false;
        reveal(item);
        ctx.click_at(item, {item->width() / 2.0, item->height() / 2.0}, Qt::LeftButton);
        lay_out();
        return true;
    };
    const auto click_named = [&](const QString& name) {
        auto* item = ctx.named(name);
        check(VerifyContext::usable(item, 16, 16));
        return click(item);
    };
    const auto browser_row = [&](const QString& name) -> QQuickItem* {
        const auto rows = controller.browserPlugins();
        for (int row = 0; row < rows.size(); ++row)
            if (rows.at(row).toMap().value("name").toString() == name)
                return ctx.named(QString("browserRow%1").arg(row));
        return nullptr;
    };
    // Keys held through the running engine, the transport stopped: the PAD
    // through the keyboard (which keeps the device running), the KICK straight
    // into the engine, as a second hand would.
    const auto hold = [&](std::size_t track, bool down) {
        if (track == 0) {
            song.selectTrack(0);
            if (down) return controller.auditionKey(60, true);
            controller.releaseAudition();
            return true;
        }
        auto* engine = controller.engine();
        if (engine == nullptr) return false;
        const PluginEvent event{down ? PluginEvent::Type::note_on : PluginEvent::Type::note_off,
                                0, 60, down ? 1.0 : 0.0};
        return engine->play_live(track, event);
    };
    // The two sides the master settles at over `blocks` blocks.
    struct Sides {
        float left = 0.0F;
        float right = 0.0F;
    };
    const auto settle = [&](int blocks) {
        for (int index = 0; index < blocks; ++index) (void)ctx.pump();
        return Sides{ctx.side_peak(0), ctx.side_peak(1)};
    };
    const auto near = [](double actual, double expected, double tolerance = 2e-3) {
        return std::abs(actual - expected) <= tolerance;
    };

    // --- The session: the CLAP synth on PAD (track 0), hard left, playing a
    // held note in the song, and on KICK (1), muted and faded, with no clip:
    // it sounds only while a key is held on it. -------------------------------
    check(controller.verifyClap(parser.value("clap-fixture")));
    controller.scanPluginPaths({parser.value("clap-fixture").toStdString(),
                                parser.value("clap-effect-fixture").toStdString()},
                               {parser.value("vst3-fixture").toStdString()},
                               {parser.value("soundfont-fixture").toStdString()});
    {
        auto configured = song.song();
        const auto synth = configured.tracks.at(0).instrument;
        configured.tracks.resize(2);
        configured.tracks[0] = Track{};
        configured.tracks[0].name = "PAD";
        configured.tracks[0].instrument = synth;
        configured.tracks[0].mix.pan = -1.0;
        configured.tracks[1] = Track{};
        configured.tracks[1].name = "KICK";
        configured.tracks[1].instrument = synth;
        configured.tracks[1].mix.mute = true;
        configured.tracks[1].mix.gain_db = -60.0;
        Pattern held(1920, 480);
        Trigger note;
        note.start = 0;
        note.duration = 1900;
        note.musical_data = Note{60, 1.0F, 0.0F};
        (void)held.add(note);
        configured.patterns = {{"HELD", held}};
        configured.clips = {{0, 0, 0, 1}};
        configured.modulators.clear();
        song.replace(std::move(configured));
        controller.flushRecompile();
    }
    check(window->setProperty("view", "ALL"));
    song.selectTrack(0);
    lay_out();
    check(song.song().tracks.size() == 2 && controller.engine() != nullptr &&
          controller.engine()->has_instrument(0) && controller.engine()->has_instrument(1));
    check(controller.auditionKey(60, true));
    controller.releaseAudition();
    (void)ctx.pump();
    reached("routing: two CLAP tracks");

    // --- 1. The CLAP effect from the browser, keyed from KICK in the rack. --
    check(click_named("browserKindEffect"));
    check(click(browser_row("Blokkily Test Effect")));
    lay_out();
    check(song.song().tracks.at(0).inserts.size() == 1 &&
          song.song().tracks.at(0).inserts.at(0).plugin.format == "CLAP");
    const int rebuilds = controller.rebuildCount();
    // The plugin declares a sidechain input, so its row carries the key.
    auto* key_chip = ctx.named("insertSidechain0");
    check(VerifyContext::usable(key_chip, 60, 16) && key_chip->isVisible());
    check(key_chip != nullptr && key_chip->property("text").toString() == "NONE");
    check(VerifyContext::usable(ctx.named("insertName0"), 40, 10));
    check(click(key_chip));
    auto* key_menu = key_chip == nullptr ? nullptr : key_chip->findChild<QObject*>("sidechainMenu0");
    check(key_menu != nullptr && key_menu->property("opened").toBool());
    check(click_named("sidechainKey0_1"));
    controller.flushRecompile();
    check(song.song().tracks.at(0).inserts.at(0).sidechain == 1u);
    check(controller.rebuildCount() == rebuilds);
    key_chip = ctx.named("insertSidechain0");
    check(key_chip != nullptr && key_chip->property("text").toString() == "KICK");
    reached("routing: the rack keys the CLAP effect from the kick");

    // Heard: PAD (0.25) through the effect (x 0.25) on the left; with the
    // muted, faded kick's key held, times 1 - 0.25.
    check(hold(0, true));
    const auto open = settle(8);
    check(near(open.left, 0.0625) && near(open.right, 0.0));
    check(hold(1, true));
    const auto ducked = settle(8);
    check(near(ducked.left, 0.0625 * 0.75));
    check(hold(1, false));
    const auto released = settle(8);
    check(near(released.left, 0.0625));
    check(hold(0, false));
    (void)settle(2);
    std::cerr << "routing: PAD through the CLAP effect " << open.left
              << ", keyed by the muted kick " << ducked.left << ", released " << released.left
              << '\n';
    reached("routing: the muted kick ducks through the plugin's sidechain input");

    // --- 2. The synth's aux output to a channel of its own, from the strip. -
    auto* add_output = ctx.named("addOutput0");
    reveal(add_output);
    check(VerifyContext::usable(add_output, 40, 18) && add_output->isVisible());
    check(click(add_output));
    auto* output_menu = add_output == nullptr ? nullptr : add_output->findChild<QObject*>("outputMenu0");
    check(output_menu != nullptr && output_menu->property("opened").toBool() &&
          output_menu->property("count").toInt() == 1);
    const auto* synth = controller.engine() == nullptr
                            ? nullptr
                            : controller.engine()->processor(track_instrument(0));
    check(click_named("addOutput0_1"));
    lay_out();
    check(song.song().tracks.size() == 3 && song.song().tracks[2].name == "PAD AUX 1" &&
          song.song().tracks[2].source == InstrumentOutput{0, 1} &&
          song.song().tracks[2].instrument.format.empty());
    // A new track is a new graph: one rebuild, the synth carried over.
    check(controller.rebuildCount() == rebuilds + 1 && controller.engine() != nullptr &&
          controller.engine()->processor(track_instrument(0)) == synth);
    auto* channel_strip = ctx.named("mixerStrip2");
    reveal(channel_strip);
    check(VerifyContext::usable(channel_strip, 180, 200));
    // The channel's own strip has no instrument, so it offers no output.
    check(ctx.named("addOutput2") != nullptr && !ctx.named("addOutput2")->isVisible());
    reached("routing: + OUT breaks the aux output out to a channel");

    // The channel hard right, from its rendered pan dial's model: the left is
    // the main output alone, the right the aux (minus half of 0.25) alone.
    song.setTrackPan(2, 1.0);
    check(hold(0, true));
    const auto split = settle(8);
    check(near(split.left, 0.0625) && near(split.right, 0.125));
    // Muted from its rendered strip: only the aux goes, live.
    const auto* engine = controller.engine();
    check(click_named("mute2"));
    const auto muted = settle(4);
    check(near(muted.left, 0.0625) && near(muted.right, 0.0));
    check(click_named("mute2"));
    // The source muted: its channel plays on.
    check(click_named("mute0"));
    const auto source_muted = settle(4);
    check(near(source_muted.left, 0.0) && near(source_muted.right, 0.125));
    check(click_named("mute0"));
    // Faded on its own fader: the aux moves alone.
    song.setTrackGain(2, -6.0);
    const auto faded = settle(4);
    check(near(faded.left, 0.0625) && near(faded.right, 0.125 * db_to_linear(-6.0)));
    check(controller.engine() == engine && controller.rebuildCount() == rebuilds + 1);
    check(hold(0, false));
    (void)settle(2);
    std::cerr << "routing: main " << split.left << ", aux channel " << split.right
              << ", aux muted " << muted.right << ", source muted " << source_muted.left << " / "
              << source_muted.right << ", aux faded " << faded.right << '\n';
    reached("routing: the aux channel's strip moves the aux signal alone, live");

    // Undo takes the channel away (and the fader and pan with it); redo
    // brings it back.
    song.selectTrack(0);
    while (song.song().tracks.size() == 3 && song.canUndo()) check(song.undo());
    lay_out();
    check(song.song().tracks.size() == 2);
    check(song.redo() && song.song().tracks.size() == 3 &&
          song.song().tracks[2].source == InstrumentOutput{0, 1});
    song.setTrackPan(2, 1.0);
    lay_out();
    reached("routing: the channel undoes and redoes");

    // --- 3. Saved, opened again, exported. ----------------------------------
    const QString project = parser.value("project");
    check(!project.isEmpty() && controller.saveProject(project));
    check(controller.loadProject(project));
    lay_out();
    check(song.song().tracks.size() == 3 &&
          song.song().tracks[2].source == InstrumentOutput{0, 1} &&
          song.song().tracks.at(0).inserts.at(0).sidechain == 1u);
    check(hold(0, true));
    const auto reloaded = settle(8);
    check(near(reloaded.left, 0.0625) && near(reloaded.right, 0.125));
    check(hold(0, false));
    (void)settle(2);
    reached("routing: the project keeps the channel and the key");

    const QString exported = parser.value("export");
    check(!exported.isEmpty() && controller.exportAudioFile(exported, "FLOAT32"));
    {
        std::string error;
        const auto wave = read_wave(exported.toStdString(), &error);
        check(wave.has_value() && wave->channels == 2);
        float left = 0.0F;
        float right = 0.0F;
        if (wave)
            for (std::size_t frame = 0; frame < wave->frames; ++frame) {
                left = std::max(left, std::abs(wave->interleaved[2 * frame]));
                right = std::max(right, std::abs(wave->interleaved[2 * frame + 1]));
            }
        // The song's note is on PAD alone (the kick has no clip, so the key
        // is silent): the main output through the effect on the left, the
        // aux channel on the right.
        check(near(left, 0.0625, 1e-4) && near(right, 0.125, 1e-4));
        std::cerr << "routing: export left " << left << " right " << right << '\n';
    }
    reached("routing: the export has the main output and the channel apart");

    // --- The picture: the PAD strip with + OUT, its channel, the keyed rack.
    song.selectTrack(0);
    lay_out();
    reveal(ctx.named("mixerStrip2"));
    VerifyContext::settle(50);
    lay_out();
    check(VerifyContext::usable(ctx.named("mixerStrip2"), 180, 200) &&
          VerifyContext::usable(ctx.named("insertSidechain0"), 60, 16) &&
          VerifyContext::usable(ctx.named("insertName0"), 40, 10) &&
          VerifyContext::usable(ctx.named("effectRack"), 180, 80));
    reached("routing: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    std::ostringstream details;
    details << " | rebuilds=" << controller.rebuildCount()
            << " | tracks=" << song.song().tracks.size() << " | "
            << controller.exportStatus().toStdString();
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("routing", run_routing);

}  // namespace
}  // namespace blokkily::verify
