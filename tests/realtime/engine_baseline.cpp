// features/audio_reliability.feature: Playback does not allocate in the render
// callback.
//
// The song engine as it stands, before any feature changes process(): a real
// CLAP instrument loaded through the production adapter plays an arrangement
// on one track while a MIDI input plays and records on another. A thousand
// process() calls cross a seek and more than a hundred loop wraps (some of them
// in the middle of a block), and not one of them may allocate or free. The
// rendered audio is then checked sample by sample, so the zero is known to
// come from an engine that really played, not from one that did nothing.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/midi/midi_input.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using blokkily::realtime::require;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;
// One tick per sample: 24000 ticks a beat at 120 BPM and 48 kHz.
constexpr blokkily::Tick song_length = 2048;
constexpr std::size_t seek_before_call = 403;
constexpr std::uint64_t seek_target = 1000;

struct Span {
    blokkily::Tick start;
    blokkily::Tick end;
};
// The arrangement track's notes. The first starts on the loop point, so every
// wrap must retrigger it; the third sits just after the seek target.
constexpr std::array<Span, 4> notes{{{0, 20}, {100, 400}, {1100, 1150}, {1900, 2000}}};

// Where the song is at a rendered sample, from nothing but the schedule of
// calls: it runs from zero, jumps to the seek target, and wraps at the end.
std::uint64_t song_position(std::size_t call, std::size_t frame) {
    const std::uint64_t played =
        call < seek_before_call ? call * block + frame
                                : seek_target + (call - seek_before_call) * block + frame;
    return played % song_length;
}

bool arrangement_sounds(std::uint64_t position) {
    for (const auto& note : notes)
        if (position >= static_cast<std::uint64_t>(note.start) &&
            position < static_cast<std::uint64_t>(note.end))
            return true;
    return false;
}

// The MIDI key goes down before call 10 of every hundred and up before call 60.
bool key_held(std::size_t call) { return call % 100 >= 10 && call % 100 < 60; }

} // namespace

BLOKKILY_REALTIME_CASE(engine_baseline) {
    using namespace blokkily;
    using blokkily::realtime::require_no_allocations;

    Pattern pattern(song_length, 24000);
    for (const auto& note : notes) {
        Trigger trigger;
        trigger.start = note.start;
        trigger.duration = note.end - note.start;
        trigger.musical_data = Note{60, 1.0F, 0.0F};
        (void)pattern.add(trigger);
    }
    Song song;
    song.patterns = {{"Baseline", std::move(pattern)}};
    song.tracks = {Track{}, Track{}};
    // Hard left and hard right, so each channel hears exactly one track.
    song.tracks[0].mix.pan = -1.0;
    song.tracks[1].mix.pan = 1.0;
    song.clips = {{0, 0, 0, 1}};

    std::string error;
    SongEngine engine;
    engine.set_instrument(0, ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH,
                                                        "dev.blokkily.test", &error));
    engine.set_instrument(1, ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH,
                                                        "dev.blokkily.test", &error));
    require(engine.has_instrument(0) && engine.has_instrument(1),
            "the CLAP fixture must load through the production adapter: " + error);
    require(engine.prepare(song, 120.0, 48000.0, block, 0, &error), "prepare: " + error);
    require(engine.song_samples() == static_cast<std::uint64_t>(song_length),
            "the song must be one tick per sample long");

    MidiInput keyboard(MidiInput::Mode::deterministic);
    require(keyboard.open(std::size_t{0}, &error), "open the deterministic input: " + error);
    keyboard.set_track(1);
    engine.connect_input(&keyboard.queue());
    engine.set_recording(true);
    engine.set_playing(true);

    // Everything the loop touches is made before the first armed call.
    std::vector<float> left(calls * block, -1.0F);
    std::vector<float> right(calls * block, -1.0F);
    const std::array<std::uint8_t, 3> key_down{0x90, 64, 100};
    const std::array<std::uint8_t, 3> key_up{0x80, 64, 0};
    std::size_t captured = 0;
    CapturedEvent event;

    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        // The control side: the keyboard and the playhead. These run on other
        // threads in the application and are outside the armed region.
        if (call == seek_before_call) engine.seek(seek_target);
        if (call % 100 == 10) require(keyboard.inject(key_down), "inject key down");
        if (call % 100 == 60) require(keyboard.inject(key_up), "inject key up");

        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        {
            test::AllocationGuard guard;
            engine.process({out_left, out_right});
            total += guard.count();
        }
        while (engine.take_captured(event)) ++captured;
    }
    require_no_allocations(total, "1000 SongEngine::process calls");

    // It really played. The arrangement track, on the left, sounds exactly
    // where the schedule of calls says the song was, across the seek and every
    // wrap, including the wraps that fall mid-block after the seek.
    std::size_t wraps = 0;
    std::size_t mid_block_wraps = 0;
    for (std::size_t call = 0; call < calls; ++call) {
        for (std::size_t frame = 0; frame < block; ++frame) {
            const auto position = song_position(call, frame);
            const bool previous_exists = call > 0 || frame > 0;
            if (previous_exists && position == 0) {
                ++wraps;
                if (frame != 0) ++mid_block_wraps;
            }
            const float heard = left[call * block + frame];
            const bool expected = arrangement_sounds(position);
            if (expected ? heard <= 0.1F : std::abs(heard) >= 1e-3F)
                throw std::runtime_error(
                    "arrangement audio is wrong at call " + std::to_string(call) + " frame " +
                    std::to_string(frame) + " (song sample " + std::to_string(position) +
                    "): " + std::to_string(heard));
        }
    }
    require(wraps >= 100 && mid_block_wraps > 0,
            "the run must cross many loop wraps, some of them mid-block");
    const std::span<const float> all_left{left};
    // Right after the seek the song is at sample 1000, and the note at 1100
    // starts a hundred samples into the block.
    require(probe::first_nonzero(all_left.subspan(seek_before_call * block, block), 1e-3F) ==
                std::optional<std::size_t>{100},
            "the seek must land on its sample");

    // The MIDI track, on the right, sounds from the block after each key goes
    // down until the block after it comes up.
    for (std::size_t call = 0; call < calls; ++call) {
        const auto rendered = std::span<const float>{right}.subspan(call * block, block);
        const bool sounding = probe::peak(rendered) > 0.1F;
        require(sounding == key_held(call) &&
                    (!sounding || probe::first_nonzero(rendered, 0.1F) == std::size_t{0}),
                "MIDI input audio is wrong at call " + std::to_string(call));
    }
    require(probe::rising_edges(std::span<const float>{right}, 0.1F) == calls / 100,
            "every key press must be heard once");
    // Recording was armed and the song playing, so each press and release
    // was captured where it sounded.
    require(captured == 2 * (calls / 100), "every input event must be captured");
}
