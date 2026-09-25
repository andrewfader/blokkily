// features/effects.feature, run on its own by the bdd_effects gate
// (`--scenario effects`). The effects in the real application, driven through
// the rendered interface and heard through the production render callback:
// the browser lists effects (the built-ins and a scanned CLAP effect) apart
// from instruments; an effect clicked in the browser is inserted after the
// selected track's instrument; bypass is live; a return fed by a send adds
// to the mix without a rebuild; the master takes inserts; what an effect was
// dialled to survives a rebuild and a save; and the bounce starts where the
// song does, the compensation trimmed.

#include "verify/harness.hpp"

#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>
#include <QVariantList>
#include <QVariantMap>

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

namespace blokkily::verify {
namespace {

// The CLAP effect fixture's state: a four-byte tag and its gain.
struct ClapEffectState {
    char tag[4];
    double gain;
};

std::vector<std::byte> clap_effect_state(double gain) {
    ClapEffectState state{};
    std::memset(&state, 0, sizeof state);
    std::memcpy(state.tag, "BKFE", 4);
    state.gain = gain;
    std::vector<std::byte> bytes(sizeof state);
    std::memcpy(bytes.data(), &state, sizeof state);
    return bytes;
}

double clap_effect_gain(const std::vector<std::byte>& bytes) {
    if (bytes.size() != sizeof(ClapEffectState)) return -1.0;
    ClapEffectState state{};
    std::memcpy(&state, bytes.data(), sizeof state);
    return std::memcmp(state.tag, "BKFE", 4) == 0 ? state.gain : -1.0;
}

void run_effects(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& controller = ctx.controller;
    const auto& parser = ctx.parser;
    const auto check = [&ctx](bool ok) { ctx.check(ok); };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    const auto click = [&ctx](QQuickItem* item) {
        if (item == nullptr) return false;
        ctx.click_at(item, {item->width() / 2.0, item->height() / 2.0}, Qt::LeftButton);
        return true;
    };
    // The level track 0 is heard at on the master bus through the production
    // callback, per unit of its strip gain: one held key on the CLAP synth
    // fixture, which plays a steady 0.25.
    const auto heard = [&]() -> float {
        song.selectTrack(0);
        (void)ctx.pump();
        if (!controller.auditionKey(60, true)) return -1.0F;
        (void)ctx.pump();
        const float peak = ctx.pump();
        controller.releaseAudition();
        (void)ctx.pump();
        (void)ctx.pump();
        const auto gain = strip_gain(song.song().tracks.at(0).mix, song.song().any_solo());
        return peak / std::max(gain.left, gain.right);
    };
    const auto near = [](float actual, float expected, float relative = 0.01F) {
        return std::abs(actual - expected) <= relative * std::abs(expected);
    };
    // The browser row showing the entry named `name`, as rendered.
    const auto browser_row = [&](const QString& name) -> QQuickItem* {
        const auto rows = controller.browserPlugins();
        for (int row = 0; row < rows.size(); ++row)
            if (rows.at(row).toMap().value("name").toString() == name)
                return ctx.named(QString("browserRow%1").arg(row));
        return nullptr;
    };
    const auto kinds_listed = [&](const QString& kind) {
        const auto rows = controller.browserPlugins();
        return std::all_of(rows.begin(), rows.end(), [&](const QVariant& row) {
            return row.toMap().value("kind").toString() == kind;
        });
    };

    // The CLAP synth on track 0; the CLAP effect scanned beside the
    // instrument fixtures, the way an installation mixes them.
    check(controller.verifyClap(parser.value("clap-fixture")));
    controller.scanPluginPaths(
        {parser.value("clap-fixture").toStdString(),
         parser.value("clap-effect-fixture").toStdString()},
        {parser.value("vst3-fixture").toStdString()},
        {parser.value("soundfont-fixture").toStdString()});
    check(ctx.window->setProperty("view", "ALL"));
    song.selectTrack(0);
    lay_out();
    const float dry = heard();
    check(near(dry, 0.25F));
    reached("effects: the CLAP synth plays track 0 at 0.25");

    // 1. The browser lists instruments and effects apart; the built-ins are
    // there without a scan. The instruments are the synth fixture, the VST3,
    // the SoundFont and the sampler's two.
    check(controller.browserKind() == "instrument" && controller.browserTotal() == 5 &&
          kinds_listed("instrument"));
    auto* effect_chip = ctx.named("browserKindEffect");
    check(VerifyContext::usable(effect_chip, 18, 18) &&
          VerifyContext::usable(ctx.named("browserKindInstrument"), 18, 18));
    check(click(effect_chip));
    lay_out();
    auto* browser = ctx.named("pluginBrowser");
    check(controller.browserKind() == "effect" && controller.browserTotal() == 5 &&
          kinds_listed("effect") && browser != nullptr &&
          browser->property("count").toInt() == 5);
    check(browser_row("Blokkily Test Effect") != nullptr && browser_row("EQ Three") != nullptr &&
          browser_row("Delay") != nullptr && browser_row("Reverb") != nullptr &&
          browser_row("Compressor") != nullptr);
    reached("effects: the browser lists effects, built-ins included");

    // 2. The rack is on screen at a usable size, pointed at track 0.
    auto* rack = ctx.named("effectRack");
    check(VerifyContext::usable(rack, 180, 80));
    check(ctx.named("effectRackTitle") != nullptr &&
          ctx.named("effectRackTitle")->property("text").toString() ==
              QString::fromStdString(song.song().tracks.at(0).name));
    reached("effects: the rack shows the selected track");

    // 3. A click on the CLAP effect inserts it after the synth: a rebuild
    // that keeps the synth running.
    const int rebuilds = controller.rebuildCount();
    const auto* synth = controller.engine() == nullptr
                            ? nullptr
                            : controller.engine()->processor(track_instrument(0));
    check(click(browser_row("Blokkily Test Effect")));
    lay_out();
    check(controller.rebuildCount() == rebuilds + 1 && controller.adoptedProcessors() == 1);
    check(song.song().tracks.at(0).inserts.size() == 1 &&
          song.song().tracks.at(0).inserts.at(0).plugin.format == "CLAP");
    check(controller.engine() != nullptr &&
          controller.engine()->processor(track_instrument(0)) == synth &&
          controller.engine()->processor({BusKind::track, 0, 0}) != nullptr);
    check(controller.outputLatency() == 64);
    check(VerifyContext::usable(ctx.named("insertRow0"), 150, 20));
    const float through = heard();
    check(near(through, 0.0625F));
    reached("effects: an inserted effect is heard");

    // 4. Bypass through the rendered rack: live, the same engine, the same
    // latency; undo takes it back, live too.
    {
        const auto* engine = controller.engine();
        check(click(ctx.named("bypass0")));
        lay_out();
        check(song.song().tracks.at(0).inserts.at(0).bypass);
        const float bypassed = heard();
        check(near(bypassed, 0.25F));
        check(controller.engine() == engine && controller.rebuildCount() == rebuilds + 1 &&
              controller.outputLatency() == 64);
        check(song.undo() && !song.song().tracks.at(0).inserts.at(0).bypass);
        lay_out();
        check(near(heard(), 0.0625F) && controller.engine() == engine &&
              controller.rebuildCount() == rebuilds + 1);
    }
    reached("effects: bypass is live and undoable");

    // 5. A return, fed by a send dialled on the rendered strip, adds to the
    // mix without a rebuild.
    check(click(ctx.named("addReturnButton")));
    // Twice: the strip grows for its send row, then the row lays itself out.
    lay_out();
    lay_out();
    check(song.song().returns.size() == 1 && controller.rebuildCount() == rebuilds + 2 &&
          controller.adoptedProcessors() == 2);
    check(VerifyContext::usable(ctx.named("returnStrip0"), 150, 80));
    {
        // The return's fader takes a press along its whole travel.
        QQuickItem* fader = nullptr;
        if (auto* dial = ctx.named("returnGain0"))
            for (auto* child : dial->childItems())
                if (child->inherits("QQuickSlider")) fader = child;
        check(VerifyContext::usable(fader, 100, 16));
    }
    auto* send_dial = ctx.named("send0_0");
    check(VerifyContext::usable(send_dial, 100, 16));
    {
        const auto* engine = controller.engine();
        // The send's own slider, pressed and dragged like any fader.
        QQuickItem* slider = send_dial;
        check(slider != nullptr && slider->inherits("QQuickSlider"));
        if (slider != nullptr) {
            // Dragged from the bottom of its travel to near the top: close
            // to 0 dB, the way a producer pulls a send up.
            const double y = slider->height() / 2.0;
            ctx.mouse_at(slider, {6.0, y}, QEvent::MouseButtonPress, Qt::LeftButton,
                         Qt::LeftButton);
            for (int step = 1; step <= 10; ++step)
                ctx.mouse_at(slider, {6.0 + (slider->width() * 0.9 - 6.0) * step / 10.0, y},
                             QEvent::MouseMove, Qt::NoButton, Qt::LeftButton);
            ctx.mouse_at(slider, {slider->width() * 0.9, y}, QEvent::MouseButtonRelease,
                         Qt::LeftButton, Qt::NoButton);
        }
        lay_out();
        const auto& sends = song.song().tracks.at(0).sends;
        check(sends.size() == 1 && sends[0].bus == 0 && !sends[0].pre_fader &&
              sends[0].level_db > -12.0);
        const float send = sends.empty() ? 0.0F
                                         : static_cast<float>(db_to_linear(sends[0].level_db));
        const float mixed = heard();
        // Direct and return, both centred: 0.0625 x (1 + send).
        check(near(mixed, 0.0625F * (1.0F + send)));
        check(controller.engine() == engine && controller.rebuildCount() == rebuilds + 2);
        std::cerr << "effects: send " << (sends.empty() ? 0.0 : sends[0].level_db)
                  << " dB, heard " << mixed << '\n';
    }
    reached("effects: a send feeds a return live");

    // 6. The master rack takes a built-in: the flat EQ leaves the level.
    const float before_master = heard();
    check(click(ctx.named("masterRack")));
    lay_out();
    check(song.masterRacked() &&
          ctx.named("effectRackTitle")->property("text").toString() == "MASTER");
    check(click(browser_row("EQ Three")));
    lay_out();
    check(song.song().master_inserts.size() == 1 &&
          song.song().master_inserts.at(0).plugin.identifier == "eq3" &&
          controller.rebuildCount() == rebuilds + 3 && controller.adoptedProcessors() == 2);
    const float after_master = heard();
    check(near(after_master, before_master));
    reached("effects: the master bus takes an insert");

    // 7. The effect's own gain moves while it runs (standing in for its own
    // window); a rebuild keeps the instance and saves what it holds.
    {
        auto* effect = controller.engine() == nullptr
                           ? nullptr
                           : controller.engine()->processor({BusKind::track, 0, 0});
        check(effect != nullptr && effect->load_state(clap_effect_state(0.5)));
        song.addTrack();
        lay_out();
        check(controller.rebuildCount() == rebuilds + 4 && controller.adoptedProcessors() == 3);
        check(controller.engine() != nullptr &&
              controller.engine()->processor({BusKind::track, 0, 0}) == effect);
        check(clap_effect_gain(song.song().tracks.at(0).inserts.at(0).plugin.state) == 0.5);
    }
    const float dialled = heard();
    const auto& sends = song.song().tracks.at(0).sends;
    const float send = sends.empty() ? 0.0F : static_cast<float>(db_to_linear(sends[0].level_db));
    check(near(dialled, 0.125F * (1.0F + send)));
    reached("effects: a rebuild keeps and saves what an effect was dialled to");

    // 8. Saved and opened again: fresh instances, the same sound.
    const QString project = parser.value("project");
    check(!project.isEmpty() && controller.saveProject(project));
    check(controller.loadProject(project));
    lay_out();
    check(song.song().tracks.at(0).inserts.size() == 1 && song.song().returns.size() == 1 &&
          song.song().master_inserts.size() == 1 && song.song().tracks.at(0).sends.size() == 1);
    check(controller.outputLatency() == 64 &&
          clap_effect_gain(song.song().tracks.at(0).inserts.at(0).plugin.state) == 0.5);
    check(near(heard(), dialled));
    reached("effects: a saved project plays its effects again");

    // 8b. An effect with work for the main thread is served there: the CLAP
    // effect on track 0 asks its host for an on_main_thread callback, and the
    // application's plugin service (the editor timer) runs it, as it does an
    // instrument's. An insert left unserved would never flush its parameters
    // while inactive or announce a new tail either.
    {
        const auto path = parser.value("clap-effect-fixture").toStdString();
        void* module = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
        check(module != nullptr);
        const auto symbol = [module](const char* name) {
            return module == nullptr ? nullptr : dlsym(module, name);
        };
        auto* request = reinterpret_cast<void (*)()>(
            symbol("blokkily_test_effect_request_callback"));
        auto* calls = reinterpret_cast<int (*)()>(symbol("blokkily_test_effect_main_thread_calls"));
        auto* instances = reinterpret_cast<int (*)()>(symbol("blokkily_test_effect_instances"));
        check(request != nullptr && calls != nullptr && instances != nullptr);
        if (request != nullptr && calls != nullptr && instances != nullptr) {
            const int live = instances();
            check(live == 1);
            // Served by one turn of the service, called directly...
            const int before = calls();
            request();
            controller.serviceEditors();
            const int direct = calls() - before;
            // ...and by the timer that runs it while an engine exists.
            request();
            VerifyContext::settle(150);
            const int timed = calls() - before - direct;
            check(direct == live && timed == live);
            std::cerr << "effects: on_main_thread served " << direct << " directly, " << timed
                      << " by the timer, for " << live << " effect instance(s)\n";
        }
        if (module != nullptr) dlclose(module);
    }
    reached("effects: an insert's main-thread callback is served");

    // 9. The bounce begins where the song does: the compensation is trimmed.
    const QString exported = parser.value("export");
    check(!exported.isEmpty() && controller.exportAudioFile(exported, "FLOAT32"));
    {
        std::string error;
        const auto wave = blokkily::read_wave(exported.toStdString(), &error);
        check(wave.has_value() && wave->channels == 2);
        if (wave) {
            std::size_t first = wave->frames;
            for (std::size_t frame = 0; frame < wave->frames; ++frame)
                if (std::abs(wave->interleaved[2 * frame]) > 1e-6F) {
                    first = frame;
                    break;
                }
            const auto* engine = controller.engine();
            const auto tail = std::max<std::uint64_t>(
                static_cast<std::uint64_t>(engine->sample_rate() / 2.0),
                engine->effect_tail_samples());
            // Track 0's first note is on the song's first tick.
            check(first == 0 && wave->frames == engine->song_samples() + tail);
            std::cerr << "effects: bounce " << wave->frames << " frames, first sound at "
                      << first << '\n';
        }
    }
    reached("effects: the bounce is trimmed of the compensation");

    // The picture: track 0's rack with its CLAP effect, the return strip and
    // its send, the browser listing effects.
    song.selectTrack(0);
    controller.setBrowserKind("effect");
    lay_out();
    // The mixer scrolled to its foot, so the return strip is in the picture.
    if (auto* mixer = ctx.named("mixerScroll")) {
        const double bottom = mixer->property("contentHeight").toDouble() - mixer->height();
        mixer->setProperty("contentY", std::max(0.0, bottom));
    }
    VerifyContext::settle(50);
    lay_out();
    check(VerifyContext::usable(ctx.named("returnStrip0"), 150, 80));
    reached("effects: screenshot state");
    check(ctx.save_screenshot());
    reached("screenshot");
    std::ostringstream details;
    details << " | rebuilds=" << controller.rebuildCount()
            << " | latency=" << controller.outputLatency()
            << " | returns=" << song.song().returns.size()
            << " | " << controller.exportStatus().toStdString();
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("effects", run_effects);

}  // namespace
}  // namespace blokkily::verify
