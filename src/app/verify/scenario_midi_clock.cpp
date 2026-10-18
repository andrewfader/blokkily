// features/midi_clock_input.feature, run by bdd_midi_clock: EXT in the
// SYNC row follows a clock sent through the MIDI input's real path: its
// Start plays the song at the clock's tempo, its pointer moves the playhead,
// its Stop stops, and the song's own tempo is never edited.
#include "verify/harness.hpp"
#include "song_model.hpp"

#include <QCoreApplication>

#include <array>
#include <cmath>
#include <iostream>
#include <source_location>

namespace blokkily::verify {
namespace {

void run_midi_clock(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "midi clock failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    const auto layout = [&] { QCoreApplication::processEvents(); (void)ctx.window->grabWindow(); };
    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    auto& midi = c.midiInput();
    if (!midi.is_open()) check(midi.open(std::size_t{0}));
    layout();
    const double own_bpm = s.song().tempo.points.front().bpm;
    const auto own_length = c.engine() ? c.engine()->song_samples() : 0;
    check(own_length > 0);
    const bool was_dirty = s.dirty();

    // Following a clock is the machine's setting, in SETTINGS.
    check(ctx.window->setProperty("view", "SETTINGS"));
    layout();
    auto* chip = ctx.named("followClock");
    check(VerifyContext::usable(chip, 30, 18));
    if (chip) ctx.click_at(chip, {chip->width() / 2, chip->height() / 2}, Qt::LeftButton);
    layout();
    check(c.followClock() && c.clockStatus() == "WAITING");

    // Another machine at 100 BPM: Start, then a beat and a half of ticks.
    std::uint64_t at = 1'000'000'000ULL;
    const std::array<std::uint8_t, 1> start{0xFA}, tick{0xF8}, stop{0xFC};
    check(midi.inject(start, at));
    const auto interval = static_cast<std::uint64_t>(60.0e9 / (100.0 * 24.0));
    for (int i = 0; i < 36; ++i) check(midi.inject(tick, at += interval));
    c.serveClock();
    layout();
    std::cerr << "following: " << c.clockStatus().toStdString() << '\n';
    check(c.clockStatus() == "100.0 BPM");
    // The readout has room to be read in full.
    auto* readout = ctx.named("clockStatus");
    check(readout && readout->property("truncated").toBool() == false);
    check(ctx.transport.playing());
    // The song is placed at the clock's tempo, without a rebuild of its
    // instruments, and its own tempo map is untouched and not dirtied.
    const auto followed_length = c.engine() ? c.engine()->song_samples() : 0;
    std::cerr << "song " << own_length << " samples at its own tempo, " << followed_length << " following\n";
    check(std::llabs(static_cast<long long>(followed_length) -
                     std::llround(static_cast<double>(own_length) * own_bpm / 100.0)) <= 2);
    check(s.song().tempo.points.front().bpm == own_bpm);
    check(s.dirty() == was_dirty);
    check(c.engine() && c.engine()->processor(blokkily::track_instrument(0)) != nullptr);
    ctx.reached("midi clock: Start plays the song at the clock's tempo");

    // Stop, then a pointer to bar 3 (32 sixteenths), then Continue.
    check(midi.inject(stop, at += interval));
    c.serveClock();
    check(!ctx.transport.playing());
    const std::array<std::uint8_t, 3> pointer{0xF2, 32, 0};
    check(midi.inject(pointer, at += interval));
    c.serveClock();
    layout();
    std::cerr << "pointer moved the playhead to tick " << ctx.transport.tick() << '\n';
    check(std::abs(ctx.transport.tick() - 32.0 * 120.0) < 1.0);
    ctx.reached("midi clock: Stop stops and a pointer moves the playhead");
    check(ctx.save_screenshot());

    // Off: the song plays at its own tempo again.
    if (chip) ctx.click_at(chip, {chip->width() / 2, chip->height() / 2}, Qt::LeftButton);
    layout();
    check(!c.followClock() && c.engine() && c.engine()->song_samples() == own_length);
    ctx.reached("midi clock: switched off, the song's own tempo is back");
    ctx.finish("an external MIDI clock followed through the input's real path");
}

[[maybe_unused]] const bool registered = register_scenario("midi_clock", run_midi_clock);

} // namespace
} // namespace blokkily::verify
