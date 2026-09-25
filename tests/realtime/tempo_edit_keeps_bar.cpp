// features/timebase.feature: A tempo edit while the song plays keeps its bar.
//
// Regression (item 1.2): changing the tempo of a playing song used to keep the
// playhead's *sample* position, so the song jumped to whatever musical position
// that sample meant under the new tempo. Halving the tempo in beat 3 of bar 1
// sent the playhead back to beat 2. The playhead must keep its musical tick
// across a clock change, which is proved here from the rendered audio: the CLAP
// fixture sounds DC while a note is held, one note per beat, and the first
// onset after the edit must land where the next beat falls under the new clock
// from the tick the playhead had reached. The render callback must not
// allocate while it moves the playhead.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace {

using blokkily::realtime::require;

constexpr std::size_t block = 256;
constexpr std::size_t blocks_before_edit = 234;   // 59904 samples: tick 1198.08
constexpr std::size_t blocks_after_edit = 160;

} // namespace

BLOKKILY_REALTIME_CASE(tempo_edit_keeps_bar) {
    using namespace blokkily;
    using blokkily::realtime::require_no_allocations;

    // Two bars of 4/4 at 480 ticks a beat, a quarter-length note on every beat.
    Pattern pattern(3840, 480);
    for (Tick beat = 0; beat < 8; ++beat) {
        Trigger trigger;
        trigger.start = beat * 480;
        trigger.duration = 240;
        trigger.musical_data = Note{60, 1.0F, 0.0F};
        (void)pattern.add(trigger);
    }
    Song song;
    song.patterns = {{"Beats", std::move(pattern)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};
    song.tempo.points = {TempoPoint{0, 120.0, false}};

    std::string error;
    SongEngine engine;
    engine.set_processor(track_instrument(0), ClapPluginInstance::create(
                                                  BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test",
                                                  &error));
    require(engine.has_instrument(0), "the CLAP fixture must load: " + error);
    require(engine.prepare(song, 48000.0, block, 0, &error), "prepare: " + error);

    std::vector<float> left(block), right(block);
    engine.set_playing(true);
    for (std::size_t call = 0; call < blocks_before_edit; ++call)
        engine.process({std::span{left}, std::span{right}});
    const std::uint64_t edit_sample = engine.sample_position();
    require(edit_sample == blocks_before_edit * block, "the engine played up to the edit");
    // 120 BPM, 480 ticks a beat, 48 kHz: 50 samples a tick.
    const double tick_at_edit = static_cast<double>(edit_sample) / 50.0;

    // Halve the tempo while the song plays.
    song.tempo.points = {TempoPoint{0, 60.0, false}};
    require(engine.recompile(song, 0, &error), "recompile: " + error);

    std::vector<float> after(blocks_after_edit * block);
    std::vector<float> after_right(blocks_after_edit * block);
    test::AllocationCount total;
    for (std::size_t call = 0; call < blocks_after_edit; ++call) {
        const std::span<float> out_left{after.data() + call * block, block};
        const std::span<float> out_right{after_right.data() + call * block, block};
        test::AllocationGuard guard;
        engine.process({out_left, out_right});
        total += guard.count();
    }
    require_no_allocations(total, "process() across a tempo change while playing");

    // At 60 BPM a tick is 100 samples. The playhead keeps tick 1198.08, so the
    // next beat (tick 1440) is (1440 - 1198.08) * 100 samples after the edit.
    const double next_beat = std::ceil(tick_at_edit / 480.0) * 480.0;
    const auto expected = static_cast<std::size_t>(std::llround((next_beat - tick_at_edit) * 100.0));
    // The block the edit lands in may still be sounding the previous note's
    // release, so the onset is the first rising edge out of silence.
    std::size_t silent_from = 0;
    while (silent_from < after.size() && after[silent_from] > 0.1F) ++silent_from;
    std::size_t onset = silent_from;
    while (onset < after.size() && after[onset] <= 0.1F) ++onset;
    require(onset < after.size(), "a beat sounded after the tempo edit");
    const auto distance = static_cast<long long>(onset) - static_cast<long long>(expected);
    if (distance < -1 || distance > 1)
        throw std::runtime_error(
            "the playhead lost its bar across the tempo edit: next beat sounded " +
            std::to_string(onset) + " samples after the edit, wanted " +
            std::to_string(expected) + " (at the old sample position it is " +
            std::to_string(static_cast<long long>(
                (std::ceil(static_cast<double>(edit_sample) / 100.0 / 480.0) * 480.0 -
                 static_cast<double>(edit_sample) / 100.0) * 100.0)) +
            ")");
    // And the engine reports the musical position it kept.
    const auto position = engine.sample_position();
    const auto wanted_position =
        static_cast<std::uint64_t>(std::llround(tick_at_edit * 100.0)) + after.size();
    require(position + 1 >= wanted_position && position <= wanted_position + 1,
            "the reported position is the kept tick under the new tempo: " +
                std::to_string(position) + " vs " + std::to_string(wanted_position));
}
