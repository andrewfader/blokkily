// features/arpeggiator.feature, run by bdd_arpeggiator: the ARP rows of the
// device panel switch the selected track's arpeggiator, the export plays it,
// one undo takes a change back, and it survives a save and a load.
#include "verify/harness.hpp"
#include "song_model.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>
#include <QFileInfo>

#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_arpeggiator(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "arpeggiator failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    const auto click = [&](QQuickItem* item) {
        if (item) ctx.click_at(item, {item->width() / 2, item->height() / 2}, Qt::LeftButton);
        layout();
    };
    check(c.verifyClap(ctx.parser.value("clap-fixture")));

    // One CLAP track holding C, E and G for a bar.
    blokkily::Song song;
    song.patterns.clear();
    song.tracks.clear();
    song.clips.clear();
    blokkily::Track track;
    track.name = "ARP";
    track.instrument = s.song().tracks[0].instrument;
    song.tracks.push_back(track);
    const auto section = song.add_section("VERSE", 1920, 480);
    const auto part = song.part(section, 0);
    check(part.has_value());
    for (const std::int16_t key : {60, 64, 67}) {
        blokkily::Trigger trigger;
        trigger.duration = 1920;
        trigger.musical_data = blokkily::Note{key, 0.9F};
        if (part) (void)song.patterns[*part].pattern.add(trigger);
    }
    check(song.place_section(section, 0) == 1);
    s.replace(std::move(song));
    c.flushRecompile();
    s.selectTrack(0);
    layout();

    const QString base = QFileInfo(ctx.parser.value("project")).path() + "/arpeggiator";
    const auto exported = [&](const QString& name) {
        const QString file = base + "-" + name + ".wav";
        check(c.exportAudioFile(file, "FLOAT32"));
        auto wave = blokkily::read_wave(file.toStdString());
        check(wave.has_value());
        return wave ? wave->interleaved : std::vector<float>{};
    };
    // Frame `at` of the left channel.
    const auto left = [](const std::vector<float>& samples, std::size_t at) {
        return 2 * at < samples.size() ? std::abs(samples[2 * at]) : 0.0F;
    };
    const auto chord = exported("off");
    check(left(chord, 1000) > 0.01F && left(chord, 4500) > 0.01F);

    // ON in the panel: the export now sounds half of each sixteenth.
    auto* enabled = ctx.named("arpEnabled");
    // Each control is usable and lies inside the device panel, and the
    // panel is no wider than the rail's column: the rows fit the rail
    // rather than widening it.
    auto* panel = ctx.named("instrumentPanel");
    auto* column = panel ? panel->parentItem() : nullptr;
    check(panel && column && panel->width() <= column->width() + 0.5);
    for (const char* name : {"arpEnabled", "arpMode", "arpLatch", "arpRate", "arpOctaves", "arpGate"}) {
        auto* item = ctx.named(name);
        check(VerifyContext::usable(item, 36, 18));
        if (item && panel)
            check(item->mapToItem(panel, QPointF(item->width(), 0)).x() <= panel->width() + 0.5);
    }
    check(enabled && enabled->property("text").toString() == "OFF");
    click(enabled);
    check(s.song().tracks[0].arp.enabled && enabled && enabled->property("on").toBool());
    const auto arpeggiated = exported("on");
    check(left(arpeggiated, 1000) > 0.01F);
    check(left(arpeggiated, 4500) == 0.0F);
    check(left(arpeggiated, 7000) > 0.01F);
    ctx.reached("arpeggiator: ON in the panel arpeggiates the export");

    // The pickers show what the model holds; LATCH is a toggle; undo is one
    // step per change.
    check(s.setArpMode(0, "DOWN") && s.setArpRate(0, "1/8") && s.setArpOctaves(0, 2));
    layout();
    check(ctx.named("arpMode")->property("value").toString() == "DOWN");
    check(ctx.named("arpRate")->property("value").toString() == "1/8");
    check(ctx.named("arpOctaves")->property("value").toString() == "2 OCT");
    check(s.setArpGate(0, 0.75));
    layout();
    check(ctx.named("arpGate")->property("value").toString() == "GATE 75%");
    click(ctx.named("arpLatch"));
    check(s.song().tracks[0].arp.latch);
    check(s.undo() && !s.song().tracks[0].arp.latch);
    check(s.redo() && s.song().tracks[0].arp.latch);
    ctx.reached("arpeggiator: settings show in the panel and undo one at a time");

    // It is saved and loaded with the song.
    const auto before = s.song().tracks[0].arp;
    check(c.saveProject(ctx.parser.value("project")));
    check(c.loadProject(ctx.parser.value("project")));
    layout();
    check(s.song().tracks[0].arp == before);
    check(ctx.named("arpMode")->property("value").toString() == "DOWN");
    ctx.reached("arpeggiator: it survives a save and a load");
    check(ctx.save_screenshot());
    ctx.finish("the track arpeggiator in the window, the export and the project");
}

[[maybe_unused]] const bool registered = register_scenario("arpeggiator", run_arpeggiator);

} // namespace
} // namespace blokkily::verify
