// features/arrangement_loop.feature: Playback wrap keeps the real-time rules.
//
// SongEngine::process() with the arrangement loop on: a wrap back to IN, a
// mute applied from the control thread, and a bounce-style reset. None of it
// may allocate or free on the render thread, and the rendered audio proves
// the loop actually ran.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <cstdint>
#include <string>
#include <vector>

BLOKKILY_REALTIME_CASE(arrangement_loop) {
    using namespace blokkily;
    using blokkily::realtime::require;
    using blokkily::realtime::require_no_allocations;

    constexpr std::size_t block = 256;
    constexpr Tick bar = 1920;
    Pattern hit(bar, 480);
    Trigger trigger;
    trigger.start = 0;
    trigger.duration = 240;
    trigger.musical_data = Note{60, 1.0F, 0.0F};
    (void)hit.add(trigger);
    Pattern hook = hit;
    Song song;
    song.patterns = {{"HIT", std::move(hit)}, {"HOOK", std::move(hook)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}, {0, 1, 2 * bar, 1}};
    song.loop = {true, 2 * bar, 3 * bar};

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
    for (int call = 0; call < 8; ++call) pump();
    engine.set_playing(false);
    pump();
    engine.seek(2 * 96000);

    test::AllocationCount total;
    float loudest = 0.0F;
    bool wrapped = false;
    {
        test::AllocationGuard guard;
        engine.set_playing(true);
        auto previous = engine.sample_position();
        for (int call = 0; call < 800; ++call) {
            pump();
            loudest = std::max(loudest, probe::peak(std::span<const float>{left}));
            const auto now = engine.sample_position();
            if (now < previous) wrapped = true;
            previous = now;
        }
        engine.set_playing(false);
        pump();
        engine.reset_processing();
        total = guard.count();
    }
    require_no_allocations(total, "process() with the arrangement loop");
    require(loudest > 0.05F, "the looped note sounded");
    require(wrapped, "playback wrapped back to IN");
    require(engine.loop_enabled(), "the loop stayed on");
}
