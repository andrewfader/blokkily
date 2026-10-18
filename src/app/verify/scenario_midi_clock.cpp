// features/midi_clock_input.feature, run by bdd_midi_clock: EXT in the
// SYNC row follows a clock sent through the MIDI input's real path: its
// Start plays the song at the clock's tempo, its pointer moves the playhead,
// its Stop stops, and the song's own tempo is never edited.
#include "verify/harness.hpp"
#include "song_model.hpp"

#include <QCoreApplication>

#include <array>
#include <cmath>
#include <atomic>
#include <vector>
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

// The fake clock the engine and the ticks share in the lock gate: time moves
// only as blocks are pumped.
std::atomic<std::uint64_t> fake_now{0};
std::uint64_t fake_clock() noexcept { return fake_now.load(std::memory_order_relaxed); }

// features/midi_clock_input.feature: the song stays on the clock's beat. A
// one-bar song with a short note on every beat follows a clock at 100.0049
// BPM, jittered by up to 0.4 ms, for two minutes of song. The follower reads
// the tempo as 100.0, which alone would let the song fall 5.9 ms behind by
// the end; the lock keeps every note within a millisecond of its beat. The
// notes are found in the rendered output and placed against the ticks on the
// same clock the engine renders by.
void run_midi_clock_lock(VerifyContext& ctx) {
    auto& c = ctx.controller;
    auto& s = ctx.song;
    const auto check = [&](bool ok, std::source_location loc = std::source_location::current()) {
        if (!ok) std::cerr << "midi clock lock failed at " << loc.line() << '\n';
        ctx.check(ok);
    };
    check(c.verifyClap(ctx.parser.value("clap-fixture")));
    auto& midi = c.midiInput();
    if (!midi.is_open()) check(midi.open(std::size_t{0}));
    // One track, one bar, a sixteenth-long note on every beat.
    blokkily::Song song;
    song.tempo = s.song().tempo;
    song.tracks = {s.song().tracks.at(0)};
    const auto per_beat = s.song().ticks_per_beat();
    blokkily::Pattern bar(4 * per_beat, per_beat);
    for (int beat = 0; beat < 4; ++beat) {
        blokkily::Trigger note;
        note.start = beat * per_beat;
        note.duration = per_beat / 4;
        note.musical_data = blokkily::Note{72, 1.0F, 0.0F};
        (void)bar.add(note);
    }
    song.patterns = {{"Beat", bar}};
    song.clips = {{0, 0, 0, 1}};
    s.restoreSong(song);
    c.flushRecompile();
    QCoreApplication::processEvents();

    constexpr double clock_bpm = 100.0049;
    constexpr double rate = 48000.0;
    constexpr std::size_t block = 1024;
    const double interval = 60.0e9 / (clock_bpm * 24.0);
    const std::uint64_t origin = 1'000'000'000ULL;
    fake_now = origin;
    const auto use_fake_clock = [&] {
        if (c.engine() != nullptr) c.engine()->set_input_clock(&fake_clock);
    };
    use_fake_clock();
    c.setFollowClock(true);
    const std::array<std::uint8_t, 1> start{0xFA}, tick{0xF8};
    check(midi.inject(start, origin));
    // Tick k at its time, jittered deterministically by up to 0.4 ms.
    std::uint64_t sent = 0;
    const auto tick_time = [&](std::uint64_t k) {
        const double jitter = 0.4e6 * std::sin(static_cast<double>(k) * 12.9898);
        return origin + static_cast<std::uint64_t>(static_cast<double>(k) * interval + jitter);
    };
    // Each block: the ticks that have arrived by its start, served, then the
    // block rendered; what it renders is heard (block + latency) later.
    struct Onset { double heard_ns; };
    std::vector<Onset> onsets;
    bool sounding = false;
    const std::size_t blocks = static_cast<std::size_t>(120.0 * rate / block);
    for (std::size_t b = 0; b < blocks; ++b) {
        while (tick_time(sent) <= fake_now) check(midi.inject(tick, tick_time(sent++)));
        c.serveClock();
        use_fake_clock();
        const auto begun = fake_now.load();
        const auto latency = c.engine() ? c.engine()->output_latency() : 0U;
        std::vector<float> stereo(block * 2, 0.0F);
        check(ctx.output && ctx.output->pump(stereo));
        for (std::size_t frame = 0; frame < block; ++frame) {
            const bool loud = std::abs(stereo[frame]) > 1e-3F;
            if (loud && !sounding)
                onsets.push_back({static_cast<double>(begun) +
                                  static_cast<double>(block + latency + frame) * 1e9 / rate});
            // A note's own zero crossings are not its end: silence is a
            // sixteenth's worth of quiet.
            if (loud) sounding = true;
        }
        float tail = 0.0F;
        for (std::size_t frame = block - 256; frame < block; ++frame)
            tail = std::max(tail, std::abs(stereo[frame]));
        if (tail <= 1e-3F) sounding = false;
        fake_now += static_cast<std::uint64_t>(static_cast<double>(block) * 1e9 / rate);
    }
    // Each onset against the beat the clock says it is on.
    double late_worst = 0.0;
    std::size_t measured = 0;
    for (const auto& onset : onsets) {
        const double clock_beat = (onset.heard_ns - static_cast<double>(origin)) / (interval * 24.0);
        // The last minute only: the lock has had time to settle.
        if (clock_beat < 100.0) continue;
        const double off_ms = (clock_beat - std::round(clock_beat)) * 60000.0 / clock_bpm;
        late_worst = std::max(late_worst, std::abs(off_ms));
        ++measured;
    }
    std::cerr << "clock lock: " << onsets.size() << " notes, " << measured
              << " in the last minute, worst " << late_worst << " ms off the clock's beat; "
              << c.clockStatus().toStdString() << '\n';
    check(c.followClock() && c.clockStatus() == "100.0 BPM");
    check(measured >= 90);
    check(late_worst < 1.0);
    ctx.reached("midi clock lock: two minutes on, every note within a millisecond of its beat");
    c.setFollowClock(false);
    ctx.finish("an external MIDI clock followed on its beat");
}

[[maybe_unused]] const bool registered = register_scenario("midi_clock", run_midi_clock);
[[maybe_unused]] const bool registered_lock = register_scenario("midi_clock_lock", run_midi_clock_lock);

} // namespace
} // namespace blokkily::verify
