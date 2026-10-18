// features/midi_output.feature, run on its own by the bdd_midi_output gate
// (`--scenario midi_output`). From the rendered MIDI OUT panel, track 0 is
// given the deterministic MIDI output on channel 10 instead of its CLAP synth,
// and the song's clock is sent to the same port. Played through the
// production render callback, the output receives the pattern's notes on
// channel 10, spaced as the steps are, and the clock: Start, a pulse every
// 24th of a beat, Stop. The strip names the port; undo gives the synth back.
// The window remembers the clock port in a settings file of the gate's own,
// never the user's, and sends the clock there again when the port is listed.

#include "verify/harness.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QMetaObject>
#include <QSettings>
#include <QTemporaryDir>

#include <algorithm>
#include <iostream>
#include <sstream>
#include <vector>

namespace blokkily::verify {
namespace {

void run_midi_output(VerifyContext& ctx) {
    auto* const window = ctx.window;
    auto& song = ctx.song;
    auto& pattern = ctx.pattern;
    auto& controller = ctx.controller;
    const auto& parser = ctx.parser;
    const auto check = [&ctx](bool ok) { ctx.check(ok); };
    const auto reached = [&ctx](const char* scenario) { ctx.reached(scenario); };
    const auto lay_out = [window] {
        (void)window->grabWindow();
        QCoreApplication::processEvents();
    };
    std::ostringstream details;

    check(controller.verifyClap(parser.value("clap-fixture")));
    controller.setTempo(120.0);
    song.selectTrack(0);
    song.selectPattern(0);
    check(window->setProperty("view", "ALL"));
    // Notes on steps 0 and 4 of track 0's part and nothing between them.
    for (int step = 0; step < 8; ++step) pattern.clearStep(step);
    pattern.setStepKey(0, 60);
    pattern.setStepKey(4, 67);
    controller.refreshMidiPorts();
    lay_out();
    check(controller.midiOutPorts() == QStringList{"Deterministic output"});
    auto* panel = ctx.named("midiOutPanel");
    auto* port_picker = ctx.named("midiOutPort");
    // The clock is the machine's, so it is set in SETTINGS; the rail beside
    // it keeps the track's own port.
    check(window->setProperty("view", "SETTINGS"));
    lay_out();
    auto* clock_picker = ctx.named("midiClockPort");
    check(VerifyContext::usable(panel, 120, 40) && VerifyContext::usable(port_picker, 60, 18) &&
          VerifyContext::usable(clock_picker, 60, 18));
    check(port_picker != nullptr && port_picker->property("value").toString() == "OFF");
    reached("midi output: the MIDI OUT panel lists the outputs");

    const auto pick = [&](const char* picker, int row) {
        auto* owner = ctx.named(QString::fromLatin1(picker));
        check(owner != nullptr && owner->isVisible());
        if (owner == nullptr) return;
        ctx.click_at(owner, {owner->width() / 2, owner->height() / 2}, Qt::LeftButton);
        lay_out();
        auto* menu = owner->findChild<QObject*>(QString::fromLatin1(picker) + "Menu");
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

    // The port, then channel 10, then the clock to the same port.
    pick("midiOutPort", 1);
    controller.flushRecompile();
    lay_out();
    const auto& slot = song.song().tracks.at(0).instrument;
    check(slot.format == "MIDI Out" && slot.identifier == "Deterministic output");
    check(controller.trackMidiOut(0) == "Deterministic output");
    auto* channel_picker = ctx.named("midiOutChannel");
    check(VerifyContext::usable(channel_picker, 30, 18));
    // Side by side, neither runs into the other.
    check(port_picker != nullptr && channel_picker != nullptr &&
          port_picker->mapToScene({port_picker->width(), 0}).x() <=
              channel_picker->mapToScene({0, 0}).x());
    pick("midiOutChannel", 9);
    check(controller.trackMidiOutChannel(0) == 9);
    pick("midiClockPort", 1);
    check(controller.midiClockPort() == "Deterministic output");
    const auto strip = song.tracks().at(0).toMap().value("instrument").toString();
    check(strip == "MIDI · Deterministic output · CH 10");
    details << " | strip " << strip.toStdString();
    reached("midi output: track 0 plays the output on channel 10 and the clock goes there too");
    check(window->setProperty("view", "ALL"));
    lay_out();

    // Played from the start for a little more than a beat, then stopped.
    auto output = controller.midiOutput("Deterministic output");
    check(output != nullptr);
    if (output != nullptr) (void)output->take_sent();
    controller.seekToStep(0);
    controller.togglePlayback();
    for (int block = 0; block < 60; ++block) (void)ctx.pump();   // 30720 samples
    controller.togglePlayback();
    (void)ctx.pump();
    std::vector<TimedMidi> sent;
    if (output != nullptr) sent = output->take_sent();
    std::vector<TimedMidi> on, pulses;
    int starts = 0, stops = 0;
    for (const auto& message : sent) {
        if (message.bytes[0] == 0x99) on.push_back(message);
        if (message.bytes[0] == 0xF8) pulses.push_back(message);
        if (message.bytes[0] == 0xFA) ++starts;
        if (message.bytes[0] == 0xFC) ++stops;
    }
    // Step 4 is a beat (24000 samples at 120 BPM) after step 0.
    check(on.size() >= 2 && on[0].bytes[1] == 60 && on[1].bytes[1] == 67 &&
          on[1].frame - on[0].frame == 24000);
    // A pulse every 1000 samples, from the first note's sample.
    bool even = pulses.size() >= 30;
    for (std::size_t index = 1; index < pulses.size() && even; ++index)
        even = pulses[index].frame - pulses[index - 1].frame == 1000;
    check(even && !pulses.empty() && !on.empty() && pulses.front().frame == on.front().frame);
    check(starts == 1 && stops == 1);
    details << " | notes " << on.size() << " | pulses " << pulses.size();
    if (!(on.size() >= 2 && even && starts == 1 && stops == 1))
        std::cerr << "midi output: " << on.size() << " notes on channel 10, " << pulses.size()
                  << " pulses (even " << even << "), " << starts << " start, " << stops
                  << " stop\n";
    reached("midi output: the notes and the clock go out as the song plays them");

    // The port and the channel are a step of history each (the clock is the
    // studio's, not the song's); undone, the synth is back.
    check(song.undo() && song.undo());
    controller.flushRecompile();
    lay_out();
    check(song.song().tracks.at(0).instrument.format == "CLAP" && controller.trackMidiOut(0).isEmpty());
    check(song.redo() && song.redo());
    controller.flushRecompile();
    lay_out();
    check(controller.trackMidiOut(0) == "Deterministic output" &&
          controller.trackMidiOutChannel(0) == 9);
    reached("midi output: undo and redo");

    // The clock is the studio's: the window remembers where it goes between
    // launches, here in a settings file of the gate's own.
    {
        const QString user_settings = QSettings().fileName();
        const QFileInfo before(user_settings);
        const bool existed = before.exists();
        const auto stamp = existed ? before.lastModified() : QDateTime{};
        QTemporaryDir studio;
        check(studio.isValid());
        const QString file = studio.filePath("studio.ini");
        const auto remembered = [&file] {
            return QSettings(file, QSettings::IniFormat).value("midi/clockPort").toString();
        };
        const auto remember = [&file](const QString& port) {
            QSettings settings(file, QSettings::IniFormat);
            settings.setValue("midi/clockPort", port);
            settings.sync();
        };
        controller.rememberMidiClock(file);
        check(window->setProperty("view", "SETTINGS"));
        lay_out();
        pick("midiClockPort", 0); // OFF
        check(controller.midiClockPort().isEmpty() && remembered().isEmpty());
        pick("midiClockPort", 1);
        check(controller.midiClockPort() == "Deterministic output" && remembered() == "Deterministic output");
        // A later launch: the clock is off until the remembered port is listed.
        check(controller.setMidiClockPort(""));
        remember("Deterministic output");
        controller.restoreMidiClock();
        check(controller.midiClockPort() == "Deterministic output");
        // Gear that is not plugged in stays remembered, and the clock stays off.
        check(controller.setMidiClockPort(""));
        remember("Unplugged synth");
        controller.restoreMidiClock();
        controller.refreshMidiPorts();
        check(controller.midiClockPort().isEmpty() && remembered() == "Unplugged synth");
        // The user's own settings were never written.
        const QFileInfo after(user_settings);
        check(after.exists() == existed && (!existed || after.lastModified() == stamp));
        check(controller.setMidiClockPort("Deterministic output"));
    }
    reached("midi output: the clock port is remembered between launches");

    lay_out();
    check(ctx.save_screenshot());
    reached("screenshot");
    ctx.finish(details.str());
}

[[maybe_unused]] const bool registered = register_scenario("midi_output", run_midi_output);

}  // namespace
}  // namespace blokkily::verify
