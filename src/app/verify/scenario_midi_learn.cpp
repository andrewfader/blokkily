// features/midi_learn.feature, run by bdd_midi_learn: a controller sent
// through the MIDI input's real decode path is learned to a fader, a mute
// and a macro, and moving it changes what the production callback renders.
#include "verify/harness.hpp"
#include "song_model.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_midi_learn(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "midi learn failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    auto& midi = c.midiInput();
    if (!midi.is_open()) check(midi.open(std::size_t{0}));
    // A knob turned: CC `controller` on channel `channel` (1-based).
    const auto turn = [&](int channel, int controller, int value) {
        const std::array<std::uint8_t, 3> message{static_cast<std::uint8_t>(0xB0 | (channel - 1)),
                                                  static_cast<std::uint8_t>(controller),
                                                  static_cast<std::uint8_t>(value)};
        check(midi.inject(message));
        c.serveControlChanges();
        layout();
    };
    // The level track 0's held note reaches the output at.
    const auto heard = [&]() -> float {
        s.selectTrack(0);
        (void)ctx.pump();
        if (!c.auditionKey(60, true)) return -1.0F;
        (void)ctx.pump();
        const float peak = ctx.pump();
        c.releaseAudition();
        (void)ctx.pump();
        (void)ctx.pump();
        return peak;
    };
    s.setTrackGain(0, 0.0);
    s.setTrackPan(0, 0.0);
    const float unity = heard();
    check(unity > 0.05F);

    // The track menu offers it; LEARN waits for a controller.
    for (const char* name : {"learnGain", "learnPan", "learnMute", "learnSolo", "forgetMidi"})
        check(ctx.window->findChild<QObject*>(name) != nullptr);
    check(c.learnMidi("gain", 0));
    check(c.midiLearning() && c.midiLearnStatus().startsWith("Move a control"));
    auto* status = ctx.named("midiLearnStatus");
    check(VerifyContext::usable(status, 100, 10));
    turn(1, 7, 127);
    check(!c.midiLearning());
    check(c.midiLearnStatus().startsWith("CC 7 · CH 1"));
    check(c.midiBindingFor("gain", 0) == "CC 7 · CH 1");
    ctx.reached("midi learn: the next controller moved is bound to the fader");

    // Turned down, the fader follows and the bus is quieter by as much.
    turn(1, 7, 64);
    const double expected_db = -60.0 + 66.0 * 64.0 / 127.0;
    check(std::abs(s.song().tracks[0].mix.gain_db - expected_db) < 1e-9);
    const float lowered = heard();
    const double measured_db = 20.0 * std::log10(lowered / unity);
    std::cerr << "fader by CC: " << measured_db << " dB heard, " << expected_db << " set\n";
    check(std::abs(measured_db - expected_db) < 0.2);
    // Another channel's CC 7 is someone else's.
    turn(2, 7, 0);
    check(std::abs(s.song().tracks[0].mix.gain_db - expected_db) < 1e-9);
    turn(1, 7, 127);
    ctx.reached("midi learn: the bound controller moves the fader, heard on the bus");

    // A mute on a button: the press that teaches it only binds it; then 127
    // mutes and 0 unmutes.
    check(c.learnMidi("mute", 0));
    turn(1, 20, 127);
    check(!s.song().tracks[0].mix.mute);
    turn(1, 20, 127);
    check(s.song().tracks[0].mix.mute);
    check(heard() < 1e-6F);
    turn(1, 20, 0);
    check(!s.song().tracks[0].mix.mute && heard() > 0.05F);
    ctx.reached("midi learn: a button mutes and unmutes the track");

    // A macro, learned from its card's LEARN chip.
    const int macro = s.addModulator("macro");
    check(macro >= 0);
    layout();
    auto* learn = ctx.named(QString("learnMacro%1").arg(macro));
    // Scrolled into view, as a producer scrolls to it.
    if (learn) {
        for (const char* name : {"editorScroll", "mixerScroll"}) {
            auto* scroll = ctx.named(name);
            bool inside = false;
            for (auto* parent = learn->parentItem(); parent; parent = parent->parentItem())
                if (parent == scroll) inside = true;
            if (!inside) continue;
            const auto y = learn->mapToItem(scroll, QPointF(0, 0)).y();
            if (y < 0 || y + learn->height() > scroll->height())
                scroll->setProperty("contentY", std::max(0.0, scroll->property("contentY").toDouble() +
                                                                  y - scroll->height() / 3.0));
            layout();
        }
    }
    check(VerifyContext::usable(learn, 40, 16));
    if (learn) ctx.click_at(learn, {learn->width() / 2, learn->height() / 2}, Qt::LeftButton);
    layout();
    check(c.midiLearning());
    turn(1, 21, 100);
    check(learn && learn->property("text").toString() == "CC 21 · CH 1");
    turn(1, 21, 127);
    check(std::abs(s.song().modulators[static_cast<std::size_t>(macro)].value - 1.0) < 1e-9);
    ctx.reached("midi learn: a macro follows its knob");

    // One step of history undoes a learn; the bindings are saved and loaded.
    const auto bindings = s.song().midi_bindings;
    check(bindings.size() == 3);
    check(c.saveProject(ctx.parser.value("project")));
    check(c.loadProject(ctx.parser.value("project")));
    check(s.song().midi_bindings == bindings);
    check(c.learnMidi("pan", 0));
    turn(1, 22, 0);
    check(s.song().midi_bindings.size() == 4);
    check(s.undo() && s.song().midi_bindings.size() == 3);
    check(c.forgetMidiBindings("gain", 0) == 1 && c.midiBindingFor("gain", 0).isEmpty());
    // Escape cancels a learn that is waiting.
    check(c.learnMidi("solo", 0));
    c.cancelMidiLearn();
    check(!c.midiLearning());
    turn(1, 23, 127);
    check(!s.song().tracks[0].mix.solo);
    ctx.reached("midi learn: undo, forget, cancel, and a save and load");
    layout();
    check(ctx.save_screenshot());
    ctx.finish("MIDI learn of a fader, a mute and a macro, heard through the callback");
}

[[maybe_unused]] const bool registered = register_scenario("midi_learn", run_midi_learn);

} // namespace
} // namespace blokkily::verify
