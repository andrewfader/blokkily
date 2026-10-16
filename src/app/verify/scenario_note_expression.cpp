// features/note_expression_editing.feature, run by bdd_note_expression: the
// piano roll's controller lane edits the selected note's own pressure and
// timbre, and one key's poly pressure, and the export plays them - read off
// the CLAP fixture, whose level is velocity x (1 + pressure) x (1 + timbre).
#include "verify/harness.hpp"
#include "song_model.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <QCoreApplication>
#include <QFileInfo>

#include <cmath>
#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_note_expression(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    auto& p = ctx.pattern;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "note expression failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    check(c.verifyClap(ctx.parser.value("clap-fixture")));

    // One CLAP note, C4, held for the whole bar.
    blokkily::Song song;
    song.patterns.clear();
    song.tracks.clear();
    song.clips.clear();
    blokkily::Track track;
    track.name = "EXPRESSIVE";
    track.instrument = s.song().tracks[0].instrument;
    song.tracks.push_back(track);
    const auto section = song.add_section("VERSE", 1920, 480);
    const auto part = song.part(section, 0);
    check(part.has_value());
    blokkily::Trigger note;
    note.duration = 1919;
    note.musical_data = blokkily::Note{60, 0.5F};
    if (part) (void)song.patterns[*part].pattern.add(note);
    check(song.place_section(section, 0) == 1);
    s.replace(std::move(song));
    c.flushRecompile();
    s.selectTrack(0);
    p.selectStep(0);
    check(ctx.window->setProperty("view", "ALL"));
    layout();

    const QString base = QFileInfo(ctx.parser.value("screenshot")).path() + "/note-expression";
    // The left channel at song tick `tick` (50 samples a tick at 120 BPM).
    const auto level_at = [&](const QString& name, int tick) {
        const QString file = base + "-" + name + ".wav";
        check(c.exportAudioFile(file, "FLOAT32"));
        const auto wave = blokkily::read_wave(file.toStdString());
        check(wave.has_value());
        const std::size_t frame = static_cast<std::size_t>(tick) * 50;
        return wave && 2 * frame < wave->interleaved.size() ? wave->interleaved[2 * frame] : -1.0F;
    };
    const float plain = level_at("plain", 600);
    check(plain > 0.01F);

    // The lane: NOTE PRESS, drawn across the note's second beat at full.
    auto* lane = ctx.named("rollControls");
    check(lane != nullptr);
    for (const char* name : {"NOTE PRESS", "NOTE TIMBRE", "NOTE PITCH", "POLY"})
        check(lane && QMetaObject::invokeMethod(lane, "pick", Q_ARG(QVariant, QString(name))));
    check(lane && QMetaObject::invokeMethod(lane, "pick", Q_ARG(QVariant, QString("NOTE PRESS"))));
    layout();
    check(lane && lane->property("laneName").toString() == "NOTE PRESS");
    p.drawControl("notepressure", 0, 480, 1.0, 959, 1.0);
    layout();
    const auto points = lane ? lane->property("lanePoints").toList() : QVariantList{};
    check(points.size() >= 30);
    check(VerifyContext::usable(ctx.named("controllerPoint0"), 1, 1));
    const float pressed = level_at("pressed", 600);
    const float before = level_at("before", 300);
    std::cerr << "note pressure: " << plain << " plain, " << pressed << " pressed, " << before << " before\n";
    check(std::abs(pressed - 2.0F * plain) < 1e-4F);
    check(std::abs(before - plain) < 1e-4F);
    ctx.reached("note expression: NOTE PRESS drawn on the note doubles it there");

    // The pressure is the note's: moved with it, and undone in one step.
    check(s.undo());
    check(std::abs(level_at("undone", 600) - plain) < 1e-4F);
    check(s.redo());
    // Erased with a right-drag's call, it is gone.
    check(p.eraseControls("notepressure", 0, 480, 960) >= 30);
    check(std::abs(level_at("erased", 600) - plain) < 1e-4F);
    // NOTE TIMBRE is the other dimension the fixture reads.
    check(QMetaObject::invokeMethod(lane, "pick", Q_ARG(QVariant, QString("NOTE TIMBRE"))));
    p.drawControl("notetimbre", 0, 480, 0.5, 959, 0.5);
    check(std::abs(level_at("timbre", 600) - 1.5F * plain) < 1e-3F);
    p.eraseControls("notetimbre", 0, 0, 1920);
    ctx.reached("note expression: undo, erase and NOTE TIMBRE");

    // POLY: the selected note's key, its pressure across the pattern.
    check(QMetaObject::invokeMethod(lane, "pick", Q_ARG(QVariant, QString("POLY"))));
    layout();
    check(lane->property("laneController").toInt() == 60);
    check(ctx.named("controllerNumber")->property("text").toString() == "C4");
    p.drawControl("poly", 60, 1200, 0.5, 1500, 0.5);
    int poly = 0;
    for (const auto& control : p.controls()) poly += control.toMap().value("kind").toString() == "poly";
    check(poly > 10);
    const float squeezed = level_at("poly", 1300);
    std::cerr << "poly pressure: " << squeezed << " against " << plain << '\n';
    check(std::abs(squeezed - plain * (1.0F + 64.0F / 127.0F)) < 1e-3F);
    ctx.reached("note expression: POLY presses one key in the pattern");

    check(QMetaObject::invokeMethod(lane, "pick", Q_ARG(QVariant, QString("NOTE PRESS"))));
    p.drawControl("notepressure", 0, 0, 0.2, 1900, 0.9);
    layout();
    check(ctx.save_screenshot());
    ctx.finish("per-note expression and poly pressure drawn in the lane, heard in the export");
}

[[maybe_unused]] const bool registered = register_scenario("note_expression", run_note_expression);

} // namespace
} // namespace blokkily::verify
