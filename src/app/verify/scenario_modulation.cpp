// features/dynamic_modulation.feature and features/sidechain.feature, run by
// the bdd_modulation gate (`--scenario modulation`). Modulation and the
// sidechain in the real application, driven through the rendered interface
// and heard through the production render callback: a compressor inserted on
// the bass is keyed from a muted, faded kick in the rack; an LFO and a macro
// are added in the modulation panel, aimed at the CLAP instrument's Level
// from the rendered target menu, shaped and turned with the rendered dials;
// the macro is heard at once without a recompile and undone; everything is
// saved, loaded and exported. Then an LFO is synced from the panel's SYNC chip
// and division picker and heard to follow a tempo change, and a follower's
// source is picked in the panel and heard to follow that track.

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
#include <vector>

namespace blokkily::verify {
namespace {

void run_modulation(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& controller = ctx.controller;
    const auto& parser = ctx.parser;
    const auto check = [&ctx](bool ok, std::source_location at = std::source_location::current()) {
        if (!ok) std::cerr << "modulation: check failed at line " << at.line() << '\n';
        ctx.check(ok);
    };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    // Scrolls whichever panel holds `item` until it is in view.
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
    // The slider inside a ValueDial, dragged from where it is to `fraction`
    // of its travel, the way a producer turns it.
    const auto drag_dial = [&](const QString& name, double fraction) {
        QQuickItem* slider = nullptr;
        if (auto* dial = ctx.named(name)) {
            reveal(dial);
            for (auto* child : dial->childItems())
                if (child->inherits("QQuickSlider")) slider = child;
        }
        check(VerifyContext::usable(slider, 100, 14));
        if (slider == nullptr) return;
        const double y = slider->height() / 2.0;
        const double from = 6.0 + (slider->width() - 12.0) *
                                      slider->property("visualPosition").toDouble();
        const double to = 6.0 + (slider->width() - 12.0) * fraction;
        ctx.mouse_at(slider, {from, y}, QEvent::MouseButtonPress, Qt::LeftButton, Qt::LeftButton);
        for (int step = 1; step <= 8; ++step)
            ctx.mouse_at(slider, {from + (to - from) * step / 8.0, y}, QEvent::MouseMove,
                         Qt::NoButton, Qt::LeftButton);
        ctx.mouse_at(slider, {to, y}, QEvent::MouseButtonRelease, Qt::LeftButton, Qt::NoButton);
        lay_out();
    };
    // Row `row` of an open picker's menu, clicked.
    const auto pick = [&](const QString& picker, int row) {
        check(click_named(picker));
        // The menu belongs to the picker that opens it.
        auto* owner = ctx.named(picker);
        auto* menu = owner == nullptr ? nullptr : owner->findChild<QObject*>(picker + "Menu");
        check(menu != nullptr && menu->property("opened").toBool());
        QQuickItem* entry = nullptr;
        if (menu != nullptr)
            QMetaObject::invokeMethod(menu, "itemAt", Q_RETURN_ARG(QQuickItem*, entry),
                                      Q_ARG(int, row));
        check(entry != nullptr);
        if (entry != nullptr) ctx.click_at(entry, {entry->width() / 2, entry->height() / 2},
                                           Qt::LeftButton);
        lay_out();
    };
    const auto strip = [&](std::size_t track) {
        const auto gain = strip_gain(song.song().tracks.at(track).mix, song.song().any_solo());
        return std::max(gain.left, gain.right);
    };
    // Keys held on the tracks through the running engine, the transport
    // stopped: the CLAP synth holds its Level while a key is down.
    const auto hold = [&](std::size_t track, bool down) {
        // The bass through the keyboard, which also keeps the device running;
        // the kick straight into the engine, as a second hand would.
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
    // The level the master settles at: the loudest sample of the last of
    // `blocks` blocks of the production callback.
    const auto settle_level = [&](int blocks) {
        float level = 0.0F;
        for (int index = 0; index < blocks; ++index) level = ctx.pump();
        return level;
    };
    const auto near = [](double actual, double expected, double relative = 0.02) {
        return std::abs(actual - expected) <= relative * std::abs(expected);
    };

    // --- The session: the CLAP synth on BASS (track 0) and on KICK (1). ---
    check(controller.verifyClap(parser.value("clap-fixture")));
    controller.scanPluginPaths({parser.value("clap-fixture").toStdString()},
                               {parser.value("vst3-fixture").toStdString()},
                               {parser.value("soundfont-fixture").toStdString()});
    {
        // BASS plays one long note through the bar; KICK has the same synth
        // and no clip, so it sounds only when a key is held on it.
        auto configured = song.song();
        const auto synth = configured.tracks.at(0).instrument;
        configured.tracks.resize(2);
        configured.tracks[0] = Track{};
        configured.tracks[0].name = "BASS";
        configured.tracks[0].instrument = synth;
        configured.tracks[1] = Track{};
        configured.tracks[1].name = "KICK";
        configured.tracks[1].instrument = synth;
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
    // A key pressed on the keyboard starts the (deterministic) device.
    check(controller.auditionKey(60, true));
    controller.releaseAudition();
    (void)ctx.pump();
    reached("modulation: two CLAP tracks");

    // --- 1. A compressor on the bass, keyed from the kick in the rack. ----
    check(click_named("browserKindEffect"));
    {
        const auto rows = controller.browserPlugins();
        QQuickItem* compressor = nullptr;
        for (int row = 0; row < rows.size(); ++row)
            if (rows.at(row).toMap().value("name").toString() == "Compressor")
                compressor = ctx.named(QString("browserRow%1").arg(row));
        check(click(compressor));
    }
    check(song.song().tracks.at(0).inserts.size() == 1 &&
          song.song().tracks.at(0).inserts.at(0).plugin.identifier == "compressor");
    // The kick is muted and pulled right down: a key is taken before the
    // fader, so it keys all the same, and it never reaches the master.
    song.toggleMute(1);
    song.setTrackGain(1, -60.0);
    lay_out();
    const int rebuilds = controller.rebuildCount();
    auto* key_chip = ctx.named("insertSidechain0");
    check(VerifyContext::usable(key_chip, 60, 16));
    check(key_chip != nullptr && key_chip->property("text").toString() == "NONE");
    check(click(key_chip));
    auto* key_menu = key_chip == nullptr ? nullptr : key_chip->findChild<QObject*>("sidechainMenu0");
    check(key_menu != nullptr && key_menu->property("opened").toBool());
    // Keying from its own track is offered but not enabled.
    auto* own = ctx.named("sidechainKey0_0");
    check(own != nullptr && !own->isEnabled());
    check(click_named("sidechainKey0_1"));
    controller.flushRecompile();
    check(song.song().tracks.at(0).inserts.at(0).sidechain == 1u);
    check(controller.rebuildCount() == rebuilds);
    // The key is named on its own line; the effect keeps room for its name.
    // (The rack's rows were made again for the new song: look it up anew.)
    key_chip = ctx.named("insertSidechain0");
    check(key_chip != nullptr && key_chip->property("text").toString() == "KICK" &&
          key_chip->property("on").toBool());
    check(VerifyContext::usable(ctx.named("insertName0"), 40, 10));
    reached("modulation: the rack keys the compressor from the kick");

    // Heard: the bass alone passes open (the silent key compresses nothing);
    // with the kick's key down it is ducked, though the kick is silent.
    check(hold(0, true));
    const float open = settle_level(6);
    const double bass = 0.25 * strip(0);
    check(near(open, bass));
    check(hold(1, true));
    const float ducked = settle_level(12);
    // 0.25 is 8 dB over the -20 dB threshold; 4:1 takes 6 dB off.
    check(near(ducked, bass * std::pow(10.0, -6.0 / 20.0), 0.05));
    check(hold(1, false));
    const float released = settle_level(40);
    check(near(released, bass));
    check(hold(0, false));
    (void)settle_level(2);
    std::cerr << "modulation: bass open " << open << ", keyed by the muted kick " << ducked
              << ", released " << released << '\n';
    reached("modulation: the muted kick ducks the bass");

    // --- 2. An LFO from the rendered panel, aimed at Level. ----------------
    auto* panel = ctx.named("modulationPanel");
    reveal(panel);
    check(VerifyContext::usable(panel, 300, 40));
    check(ctx.named("modulationEmpty") != nullptr && ctx.named("modulationEmpty")->isVisible());
    const int recompiles = controller.recompileCount();
    check(click_named("addLfo"));
    controller.flushRecompile();
    check(song.song().modulators.size() == 1 &&
          song.song().modulators[0].kind == Modulator::Kind::lfo &&
          controller.rebuildCount() == rebuilds);
    check(VerifyContext::usable(ctx.named("modulator0"), 300, 50));
    check(click_named("addTarget0"));
    {
        auto* chip = ctx.named("addTarget0");
        auto* menu = chip == nullptr ? nullptr : chip->findChild<QObject*>("targetMenu0");
        check(menu != nullptr && menu->property("opened").toBool() &&
              menu->property("count").toInt() >= 4);
        // The first choice is the bass's instrument Level.
        auto* level = ctx.named("targetChoice0_0");
        check(level != nullptr && level->property("text").toString() == "INSTR · Level");
        check(click(level));
    }
    controller.flushRecompile();
    check(song.song().modulators[0].targets.size() == 1 &&
          song.song().modulators[0].targets[0].processor == track_instrument(0) &&
          song.song().modulators[0].targets[0].parameter_index == 0);
    check(controller.recompileCount() > recompiles && controller.rebuildCount() == rebuilds);
    check(VerifyContext::usable(ctx.named("modDepth0_0"), 200, 30));
    // A square, at a few hertz, a fifth of Level's range deep.
    pick("modulatorShape0", 4);
    check(song.song().modulators[0].shape == LfoShape::square);
    drag_dial("modulatorRate0", 0.2);
    drag_dial("modDepth0_0", 0.6);
    const auto& shaped = song.song().modulators[0];
    check(shaped.rate_hz > 2.0 && shaped.rate_hz < 6.0 && std::abs(shaped.targets[0].depth - 0.2) < 0.08);
    const double depth = shaped.targets[0].depth;
    check(hold(0, true));
    float lowest = 1.0F;
    float highest = 0.0F;
    for (int block = 0; block < 60; ++block) {
        const float level = ctx.pump();
        if (block < 4) continue;
        lowest = std::min(lowest, level);
        highest = std::max(highest, level);
    }
    check(near(highest, (0.25 + depth) * strip(0), 0.03) &&
          near(lowest, (0.25 - depth) * strip(0), 0.05));
    std::cerr << "modulation: LFO " << shaped.rate_hz << " Hz, depth " << depth << ", heard "
              << lowest << " .. " << highest << '\n';
    reached("modulation: an LFO from the panel moves the CLAP Level");

    // Removed from the panel: the offset goes with it.
    check(click_named("removeModulator0"));
    controller.flushRecompile();
    check(song.song().modulators.empty());
    check(near(settle_level(4), bass));
    reached("modulation: removing the LFO lets go of Level");

    // --- 3. A macro, turned live. -----------------------------------------
    check(click_named("addMacro"));
    check(click_named("addTarget0"));
    check(click_named("targetChoice0_0"));
    controller.flushRecompile();
    check(song.song().modulators.size() == 1 &&
          song.song().modulators[0].kind == Modulator::Kind::macro &&
          song.song().modulators[0].targets.size() == 1);
    check(near(settle_level(4), bass));
    {
        const auto* engine = controller.engine();
        const int before = controller.recompileCount();
        drag_dial("macroValue0", 0.8);
        const double value = song.song().modulators[0].value;
        check(value > 0.7 && value < 0.9);
        // Heard in the very next block: a live move, no recompile, no rebuild.
        const float turned = ctx.pump();
        const double expected = (0.25 + 0.5 * value) * strip(0);
        check(near(turned, expected));
        check(controller.engine() == engine && controller.recompileCount() == before &&
              controller.rebuildCount() == rebuilds);
        std::cerr << "modulation: macro " << value << " heard " << turned << " (expected "
                  << expected << ")\n";
        // Undo takes the turn back, live too.
        check(song.undo() && song.song().modulators[0].value == 0.0);
        lay_out();
        check(near(ctx.pump(), bass));
        check(song.redo() && std::abs(song.song().modulators[0].value - value) < 1e-9);
        lay_out();
        check(near(ctx.pump(), expected));
    }
    check(hold(0, false));
    (void)settle_level(2);
    reached("modulation: a macro is heard at once and undoes");

    // --- 4. Saved, opened again, exported. ---------------------------------
    const auto macro_value = song.song().modulators[0].value;
    const QString project = parser.value("project");
    check(!project.isEmpty() && controller.saveProject(project));
    check(controller.loadProject(project));
    lay_out();
    check(song.song().modulators.size() == 1 &&
          std::abs(song.song().modulators[0].value - macro_value) < 1e-12 &&
          song.song().tracks.at(0).inserts.at(0).sidechain == 1u);
    check(hold(0, true));
    check(near(settle_level(4), (0.25 + 0.5 * macro_value) * strip(0)));
    check(hold(0, false));
    (void)settle_level(2);
    reached("modulation: the project keeps its modulator and key");

    const QString exported = parser.value("export");
    check(!exported.isEmpty() && controller.exportAudioFile(exported, "FLOAT32"));
    {
        std::string error;
        const auto wave = read_wave(exported.toStdString(), &error);
        check(wave.has_value() && wave->channels == 2);
        float loudest = 0.0F;
        if (wave)
            for (const float sample : wave->interleaved) loudest = std::max(loudest, std::abs(sample));
        // The song's notes are on the bass alone (the kick has no clip), so
        // the compressor's key is silent and the export sounds the macro'd
        // Level through the strip.
        const double expected = (0.25 + 0.5 * macro_value) * strip(0);
        check(near(loudest, expected));
        std::cerr << "modulation: export peak " << loudest << " (expected " << expected << ")\n";
    }
    reached("modulation: the export has the modulation");

    // --- 5. A tempo-synced LFO, from the panel's SYNC and division. --------
    // The macro goes; a square LFO is aimed at the bass's Level, a fifth of
    // its range deep. (Opening the project rebuilt the engine: counted from
    // here.)
    const int reopened = controller.rebuildCount();
    check(click_named("removeModulator0"));
    song.selectTrack(0);
    lay_out();
    check(click_named("addLfo"));
    check(click_named("addTarget0"));
    check(click_named("targetChoice0_0"));
    pick("modulatorShape0", 4);
    drag_dial("modDepth0_0", 0.6);
    controller.flushRecompile();
    check(song.song().modulators.size() == 1 &&
          song.song().modulators[0].kind == Modulator::Kind::lfo &&
          song.song().modulators[0].sync_beats == 0.0 &&
          std::abs(song.song().modulators[0].targets.at(0).depth - 0.2) < 0.08);
    {
        const int before = controller.recompileCount();
        const auto* engine = controller.engine();
        check(click_named("modulatorSync0"));
        // Synced: the rate dial gives way to the division picker.
        check(song.song().modulators[0].sync_beats == 1.0);
        check(ctx.named("modulatorRate0") != nullptr && !ctx.named("modulatorRate0")->isVisible());
        check(VerifyContext::usable(ctx.named("modulatorDivision0"), 40, 18) &&
              ctx.named("modulatorDivision0")->property("value").toString() == "1/4");
        pick("modulatorDivision0", 1);
        check(song.song().modulators[0].sync_beats == 0.5 &&
              ctx.named("modulatorDivision0")->property("value").toString() == "1/8");
        // A live move, like the rate: no recompile, no rebuild.
        check(controller.recompileCount() == before && controller.engine() == engine &&
              controller.rebuildCount() == reopened);
    }
    // Heard with the transport stopped: an eighth-note square at 120 BPM
    // turns up every 12000 samples, 23.4 blocks of 512; at 60 BPM every 46.9.
    const auto rising_spacing = [&](int blocks) {
        std::vector<int> rising;
        bool high = true;
        const double threshold = 0.25 * strip(0);
        for (int block = 0; block < blocks; ++block) {
            const bool now = ctx.pump() > threshold;
            if (now && !high && block > 0) rising.push_back(block);
            high = now;
        }
        if (rising.size() < 2) return 0.0;
        return static_cast<double>(rising.back() - rising.front()) /
               static_cast<double>(rising.size() - 1);
    };
    check(hold(0, true));
    const double at_120 = rising_spacing(120);
    check(std::abs(at_120 - 12000.0 / 512.0) < 1.5);
    controller.setTempo(60.0);
    controller.flushRecompile();
    const double at_60 = rising_spacing(240);
    check(std::abs(at_60 - 24000.0 / 512.0) < 1.5);
    controller.setTempo(120.0);
    controller.flushRecompile();
    check(hold(0, false));
    (void)settle_level(2);
    check(controller.rebuildCount() == reopened);
    std::cerr << "modulation: synced 1/8 LFO turns up every " << at_120 << " blocks at 120 BPM, "
              << at_60 << " at 60 BPM\n";
    reached("modulation: a synced LFO from the panel follows the tempo");

    // --- 6. A follower whose source is picked in the panel. ----------------
    // The compressor sits out (bypassed from the rack), so what the kick does
    // to the bass here is the follower's alone.
    check(click_named("removeModulator0"));
    check(click_named("bypass0"));
    check(song.song().tracks.at(0).inserts.at(0).bypass);
    song.selectTrack(0);
    lay_out();
    check(click_named("addFollower"));
    check(click_named("addTarget0"));
    check(click_named("targetChoice0_0"));
    controller.flushRecompile();
    check(song.song().modulators.size() == 1 &&
          song.song().modulators[0].kind == Modulator::Kind::follower &&
          song.song().modulators[0].source_track == 0 &&
          song.song().modulators[0].targets.size() == 1);
    check(VerifyContext::usable(ctx.named("followerSource0"), 40, 18) &&
          ctx.named("followerSource0")->property("value").toString() == "BASS");
    {
        const int before = controller.recompileCount();
        // The second track in the rendered picker's menu: the kick.
        pick("followerSource0", 1);
        controller.flushRecompile();
        check(song.song().modulators[0].source_track == 1 &&
              ctx.named("followerSource0")->property("value").toString() == "KICK");
        check(controller.recompileCount() > before && controller.rebuildCount() == reopened);
    }
    // Heard: the bass alone at its Level; with a key held on the muted,
    // faded kick, the follower follows the kick's pre-fader 0.25 and adds
    // half of it to the bass's Level; let go, the bass falls back.
    check(hold(0, true));
    const float alone = settle_level(8);
    check(near(alone, bass));
    check(hold(1, true));
    const float following = settle_level(20);
    check(near(following, (0.25 + 0.5 * 0.25) * strip(0), 0.03));
    check(hold(1, false));
    const float fallen = settle_level(60);
    check(near(fallen, bass, 0.03));
    check(hold(0, false));
    (void)settle_level(2);
    std::cerr << "modulation: follower of the kick: bass " << alone << ", kick held "
              << following << ", let go " << fallen << '\n';
    reached("modulation: a follower picked in the panel follows its source");

    // --- The picture: the rack's keyed compressor, the follower and a
    // synced LFO. --------------------------------------------------------------
    song.selectTrack(0);
    check(song.addModulator("lfo") == 1 &&
          song.addModulationTarget(1, "track", 0, 0, 0));
    song.setModulatorDivision(1, "1/8");
    controller.flushRecompile();
    lay_out();
    reveal(ctx.named("modulator1"));
    VerifyContext::settle(50);
    lay_out();
    check(VerifyContext::usable(ctx.named("modulationPanel"), 300, 120) &&
          VerifyContext::usable(ctx.named("modDepth1_0"), 200, 30) &&
          VerifyContext::usable(ctx.named("modulatorDivision1"), 40, 18) &&
          VerifyContext::usable(ctx.named("modulatorSync1"), 40, 18) &&
          VerifyContext::usable(ctx.named("followerSource0"), 40, 18) &&
          VerifyContext::usable(ctx.named("insertSidechain0"), 60, 16) &&
          VerifyContext::usable(ctx.named("insertName0"), 40, 10));
    reached("modulation: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    std::ostringstream details;
    details << " | rebuilds=" << controller.rebuildCount()
            << " | modulators=" << song.song().modulators.size() << " | "
            << controller.exportStatus().toStdString();
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("modulation", run_modulation);

}  // namespace
}  // namespace blokkily::verify
