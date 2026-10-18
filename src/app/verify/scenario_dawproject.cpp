// features/dawproject.feature, run by the bdd_dawproject gate: the rail's
// DAWPROJECT chip, and a .dawproject written by the real window from the
// song it plays, read back and compared with what the engine plays.
#include "verify/harness.hpp"
#include "song_model.hpp"
#include "blokkily/project/dawproject.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QQuickItem>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <source_location>
#include <tuple>

namespace blokkily::verify {
namespace {

void run_dawproject(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "dawproject failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    check(c.verifyClap(ctx.parser.value("clap-fixture")));

    // Two CLAP tracks in one section placed twice, a return fed by a send.
    blokkily::Song song;
    song.patterns.clear();
    song.tracks.clear();
    song.clips.clear();
    const auto instrument = s.song().tracks[0].instrument;
    for (const char* name : {"Lead", "Bass"}) {
        blokkily::Track track;
        track.name = name;
        track.instrument = instrument;
        song.tracks.push_back(track);
    }
    song.tracks[0].mix.pan = -0.4;
    song.tracks[1].mix.gain_db = -5.0;
    song.returns.resize(1);
    song.returns[0].name = "Room";
    song.tracks[0].sends.push_back({0, -8.0, false});
    const auto section = song.add_section("VERSE", 1920, 480);
    for (std::size_t t = 0; t < 2; ++t) {
        const auto slot = song.part(section, t);
        check(slot.has_value());
        if (!slot) continue;
        for (int step = 0; step < 4; ++step) {
            blokkily::Trigger trigger;
            trigger.start = step * 480;
            trigger.duration = 360;
            trigger.musical_data = blokkily::Note{static_cast<std::uint8_t>((t == 0 ? 64 : 40) + step * 2), 0.8F};
            (void)song.patterns[*slot].pattern.add(trigger);
        }
    }
    check(song.place_section(section, 0) == 2);
    check(song.place_section(section, 1920) == 2);
    s.replace(std::move(song));
    c.flushRecompile();
    check(c.saveProject(ctx.parser.value("project")));
    layout();

    // The rail: the chip sits under MIDI IN and MIDI OUT, wide enough for its
    // whole label, and leaves them theirs.
    for (const char* name : {"importMidiButton", "exportMidiButton"})
        check(VerifyContext::usable(ctx.named(name), 60, 16));
    auto* chip = ctx.named("exportDawProjectButton");
    check(VerifyContext::usable(chip, 120, 18));
    check(chip && chip->property("text").toString() == "DAWPROJECT");
    if (chip) {
        // The label is drawn whole, not elided.
        int labels = 0;
        for (auto* child : chip->findChildren<QQuickItem*>())
            if (child->property("text").toString() == "DAWPROJECT" && child->property("truncated").isValid()) {
                ++labels;
                check(!child->property("truncated").toBool());
            }
        check(labels == 1);
    }
    ctx.reached("dawproject: the DAWPROJECT chip on the rail");

    // The window writes the file from the song it plays.
    const QString file = ctx.parser.value("export");
    QDir().mkpath(QFileInfo(file).path());
    QFile::remove(file);
    check(c.exportDawProjectFile(file));
    std::cerr << c.exportStatus().toStdString() << '\n';
    check(c.exportStatus().startsWith(QFileInfo(file).fileName() + " · 2 tracks · 16 notes · 2 plugin states"));
    check(c.dawProjectOmissions().isEmpty());

    std::string error;
    const auto read = blokkily::read_dawproject(file.toStdString(), &error);
    check(read.has_value());
    if (read) {
        const auto& played = s.song();
        check(read->tracks.size() == 4);
        for (std::size_t t = 0; t < played.tracks.size() && t < read->tracks.size(); ++t) {
            const auto& track = read->tracks[t];
            check(track.name == played.tracks[t].name);
            check(track.clips.size() == 2);
            // Each note where the engine plays it.
            std::vector<std::tuple<long long, long long, int>> exported, expected;
            for (const auto& clip : track.clips)
                for (const auto& note : clip.notes)
                    exported.emplace_back(std::llround((clip.time + note.time) * 480.0),
                                          std::llround(note.duration * 480.0), note.key);
            for (const auto& note : played.arrange(t, 0).notes)
                expected.emplace_back(note.start, note.duration, note.key);
            std::sort(exported.begin(), exported.end());
            std::sort(expected.begin(), expected.end());
            check(!expected.empty() && exported == expected);
            // The instrument's state is the running instance's.
            check(track.channel.devices.size() == 1);
            if (!track.channel.devices.empty()) {
                const auto& device = track.channel.devices[0];
                check(device.element == "ClapPlugin" && device.device_id == "dev.blokkily.test");
                check(!device.state.empty() && device.state == played.tracks[t].instrument.state);
            }
        }
        check(std::abs(read->tracks[0].channel.pan - 0.3) < 1e-9);
        check(std::abs(read->tracks[1].channel.volume - std::pow(10.0, -5.0 / 20.0)) < 1e-9);
        check(read->tracks[0].channel.sends.size() == 1 &&
              read->tracks[0].channel.sends[0].destination == read->tracks[2].channel.id);
    }
    ctx.reached("dawproject: the window's export reads back as the engine plays");

    layout();
    check(VerifyContext::usable(ctx.named("exportStatus"), 100, 10));
    check(ctx.save_screenshot());
    ctx.finish("a .dawproject from the window, read back against the engine");
}

[[maybe_unused]] const bool registered = register_scenario("dawproject", run_dawproject);

} // namespace
} // namespace blokkily::verify
