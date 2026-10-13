// features/midi_output.feature: sending a track to outboard gear and sending
// MIDI clock cost the audio thread nothing.
//
// A MIDI Out instrument plays a pattern of notes and controller movements to
// a deterministic output while the engine sends clock to another, across
// stops, seeks and loop wraps. A thousand process() calls may not allocate
// or free, and what went out must be what the song played.

#include "realtime_case.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/midi/midi_out_instrument.hpp"
#include "blokkily/midi/midi_output.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

using blokkily::realtime::require;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;
std::atomic<std::uint64_t> now{3'000'000'000};
std::uint64_t test_clock() noexcept { return now.load(std::memory_order_relaxed); }

} // namespace

BLOKKILY_REALTIME_CASE(midi_output) {
    using namespace blokkily;
    using blokkily::realtime::require_no_allocations;

    Pattern pattern(4096, 24000);
    for (Tick at = 0; at < 4096; at += 512) {
        Trigger trigger;
        trigger.start = at;
        trigger.duration = 200;
        trigger.musical_data = Note{static_cast<std::int16_t>(60 + at / 512), 0.8F};
        (void)pattern.add(trigger);
    }
    pattern.add_continuous({1000, ContinuousEvent::Kind::control_change, 1, 90});
    Song song;
    song.patterns = {{"Out", std::move(pattern)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};

    auto notes = std::make_shared<MidiOutput>(MidiOutput::Mode::deterministic);
    MidiOutput clock(MidiOutput::Mode::deterministic);
    require(notes->open("Deterministic output") && clock.open("Deterministic output"),
            "the deterministic outputs open");
    std::string error;
    SongEngine engine;
    engine.set_instrument(0, std::make_unique<MidiOutInstrument>(notes, 0));
    require(engine.prepare(song, 48000.0, block, 0, &error), "prepare: " + error);
    engine.set_input_clock(&test_clock);
    engine.set_clock_output(&clock);
    engine.set_playing(true);

    std::vector<float> left(block), right(block);
    std::vector<TimedMidi> sent, clocked;
    sent.reserve(8192);
    clocked.reserve(8192);
    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        // The control side, outside the armed region.
        if (call == 300) engine.set_playing(false);
        if (call == 310) {
            engine.seek(1500);
            engine.set_playing(true);
        }
        now += 5'333'333;
        {
            test::AllocationGuard guard;
            engine.process({left, right});
            total += guard.count();
        }
        if (call % 50 == 49) {
            for (const auto& message : notes->take_sent()) sent.push_back(message);
            for (const auto& message : clock.take_sent()) clocked.push_back(message);
        }
    }
    engine.set_input_clock(nullptr);
    require_no_allocations(total, "1000 SongEngine::process calls sending MIDI and clock");

    std::size_t note_ons = 0, controls = 0, pulses = 0, starts = 0, stops = 0, positions = 0;
    for (const auto& message : sent) {
        if (message.bytes[0] == 0x90) ++note_ons;
        if (message.bytes[0] == 0xB0 && message.bytes[1] == 1 && message.bytes[2] == 90) ++controls;
    }
    for (const auto& message : clocked) {
        if (message.bytes[0] == 0xF8) ++pulses;
        if (message.bytes[0] == 0xFA) ++starts;
        if (message.bytes[0] == 0xFC) ++stops;
        if (message.bytes[0] == 0xF2) ++positions;
    }
    // About 990 blocks of a 4096-sample song: some sixty laps, eight notes a
    // lap, a pulse every 1000 samples.
    require(note_ons > 400 && controls > 50, "the notes and the wheel went out every lap");
    require(starts == 1 && stops >= 1 && positions >= 1 && pulses > 200,
            "the clock was started once, stopped, repositioned on the seek and the laps, and "
            "pulsed throughout");
}
