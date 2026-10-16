// features/pattern_swing.feature: Playback of a swung pattern keeps the
// real-time rules.
//
// SongEngine::process() with swing at 100%: the even sixteenth sounds late,
// and none of it may allocate or free on the render thread.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <cstdint>
#include <string>
#include <vector>

BLOKKILY_REALTIME_CASE(pattern_swing) {
    using namespace blokkily;
    using blokkily::realtime::require;
    using blokkily::realtime::require_no_allocations;

    constexpr std::size_t block = 256;
    Pattern pattern(1920, 480);
    pattern.set_swing(100);
    Trigger downbeat;
    downbeat.start = 0;
    downbeat.duration = 60;
    downbeat.musical_data = Note{60, 1.0F, 0.0F};
    (void)pattern.add(downbeat);
    Trigger offbeat = downbeat;
    offbeat.start = 120;
    offbeat.musical_data = Note{64, 1.0F, 0.0F};
    (void)pattern.add(offbeat);
    Song song;
    song.patterns = {{"SWING", std::move(pattern)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};

    std::string error;
    SongEngine engine;
    engine.set_processor(track_instrument(0), ClapPluginInstance::create(
                                                  BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test",
                                                  &error));
    require(engine.has_instrument(0), "the CLAP fixture must load: " + error);
    require(engine.prepare(song, 48000.0, block, 0, &error), "prepare: " + error);

    std::vector<float> left(block), right(block);
    const auto pump = [&] { engine.process({std::span{left}, std::span{right}}); };
    engine.set_playing(true);
    for (int call = 0; call < 4; ++call) pump();

    test::AllocationCount total;
    float downbeat_peak = 0.0F;
    float straight_offbeat = 0.0F;
    float delayed_offbeat = 0.0F;
    {
        test::AllocationGuard guard;
        engine.seek(0);
        engine.set_playing(true);
        std::uint64_t at = 0;
        for (int call = 0; call < 80; ++call) {
            pump();
            const float loud = probe::peak(std::span<const float>{left});
            if (at < 1000) downbeat_peak = std::max(downbeat_peak, loud);
            if (at >= 6000 && at < 7000) straight_offbeat = std::max(straight_offbeat, loud);
            if (at >= 9000 && at < 10000) delayed_offbeat = std::max(delayed_offbeat, loud);
            at += block;
        }
        engine.set_playing(false);
        pump();
        total = guard.count();
    }
    require_no_allocations(total, "process() with pattern swing");
    require(downbeat_peak > 0.05F, "the downbeat sounded");
    require(straight_offbeat < 1e-4F, "the even sixteenth was silent on the grid");
    require(delayed_offbeat > 0.05F, "and sounded half a sixteenth later");
}
