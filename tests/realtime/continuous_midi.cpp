// features/continuous_midi.feature: controller movements (wave 4.1) play and
// are chased in the render callback without allocating. A pattern full of
// wheel and mod-wheel movements plays on the CLAP fixture through the
// production adapter across loop wraps and seeks, each of which chases every
// controller, while a keyboard bends and presses on the same track.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/midi/midi_input.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <array>
#include <string>
#include <vector>

namespace {

using namespace blokkily;
using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;

BLOKKILY_REALTIME_CASE(continuous_midi) {
    Song song;
    Pattern pattern(480, 480);   // half a second a loop: many wraps
    Trigger note;
    note.duration = 400;
    note.musical_data = Note{69, 0.9F, 0.0F, 0.0};
    (void)pattern.add(note);
    for (Tick tick = 0; tick < 480; tick += 10) {
        pattern.add_continuous({tick, ContinuousEvent::Kind::pitch_bend, 0,
                                static_cast<std::uint16_t>((tick * 97) % 16384)});
        pattern.add_continuous({tick + 5, ContinuousEvent::Kind::control_change, 1,
                                static_cast<std::uint16_t>(tick % 128)});
    }
    song.patterns = {{"P", std::move(pattern)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};

    SongEngine engine;
    std::string error;
    engine.set_instrument(0, ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH,
                                                        "dev.blokkily.test", &error));
    require(engine.has_instrument(0), "the CLAP fixture loads: " + error);
    require(engine.prepare(song, 48000.0, block, 0, &error), "prepare: " + error);
    MidiInput keyboard{MidiInput::Mode::deterministic};
    require(keyboard.open(std::size_t{0}, &error), "open the keyboard: " + error);
    keyboard.set_track(0);
    engine.connect_input(&keyboard.queue());
    engine.set_playing(true);

    std::vector<float> left(block), right(block);
    std::vector<float> heard;
    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        if (call % 97 == 0) engine.seek((call * 131) % 24000);
        if (call % 13 == 0) {
            const std::array<std::uint8_t, 3> wheel{0xE5, static_cast<std::uint8_t>(call % 128), 0x50};
            const std::array<std::uint8_t, 3> pressure{0xD5, static_cast<std::uint8_t>(call % 128), 0};
            require(keyboard.inject(wheel) && keyboard.inject(pressure), "the keyboard plays");
        }
        test::AllocationGuard guard;
        engine.process({left, right});
        total += guard.count();
        if (call < 8) heard.insert(heard.end(), left.begin(), left.end());
    }
    require_no_allocations(total, "1000 process calls playing and chasing controller movements");
    require(probe::peak(heard) > 0.1F, "the track plays");
}

} // namespace
