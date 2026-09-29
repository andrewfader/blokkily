// features/scene_launcher.feature: The launcher keeps the real-time rules.
//
// Two CLAP fixtures play launched cells while scenes are launched on bar and
// beat boundaries, follow actions move between scenes, a cell is stopped, the
// song wraps under the loops and the playhead is moved, the song is
// recompiled (a pattern edited) from the control side, and arrangement
// recording hands takes back. None of the process() calls may allocate or
// free, and the rendered audio proves the launcher really played.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <string>
#include <vector>

namespace {

using namespace blokkily;
using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

constexpr std::size_t calls = 3000;
constexpr std::size_t block = 256;
constexpr double rate = 48000.0;

Pattern make_pattern(std::int16_t key, float velocity) {
    Pattern pattern(1920, 480);
    for (Tick at = 0; at < 1920; at += 480) {
        Trigger trigger;
        trigger.start = at;
        trigger.duration = 400;
        trigger.musical_data = Note{key, velocity, 0.0F};
        (void)pattern.add(trigger);
    }
    return pattern;
}

} // namespace

BLOKKILY_REALTIME_CASE(scene_launcher) {
    Song song;
    // Three bars of song: it wraps under the launched loops.
    song.patterns = {{"EMPTY", Pattern(1920, 480)}, {"P0", make_pattern(60, 0.8F)},
                     {"P1", make_pattern(64, 0.5F)}};
    song.tracks = {Track{}, Track{}};
    song.clips = {{0, 0, 0, 3}};
    song.launcher.add_scene("Scene 1");
    song.launcher.add_scene("Scene 2");
    song.launcher.set_slot(0, 0, {.pattern = 1, .repeats = 2, .follow_action = FollowAction::next});
    song.launcher.set_slot(0, 1, {.pattern = 2, .repeats = 2, .follow_action = FollowAction::next});
    song.launcher.set_slot(1, 0, {.pattern = 2, .repeats = 2, .follow_action = FollowAction::again});
    song.launcher.set_slot(1, 1, {.pattern = 1, .repeats = 1, .quantization = LaunchQuantization::beat,
                                  .follow_action = FollowAction::random});

    SongEngine engine;
    std::string error;
    for (std::size_t track = 0; track < 2; ++track)
        engine.set_instrument(track, ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH,
                                                                "dev.blokkily.test", &error));
    require(engine.has_instrument(0) && engine.has_instrument(1),
            "the CLAP fixture loads through the production adapter: " + error);
    require(engine.prepare(song, rate, block, 0, &error), "engine prepare: " + error);
    engine.set_launcher_recording(true);
    require(engine.launch_scene(0), "launch queued");
    engine.set_playing(true);

    std::vector<float> left(calls * block, 0.0F);
    std::vector<float> right(calls * block, 0.0F);
    auto edited = song;
    edited.patterns[1].pattern = make_pattern(62, 0.9F);
    std::size_t takes = 0;
    LauncherTake take;
    test::AllocationCount total;

    for (std::size_t call = 0; call < calls; ++call) {
        // The control side, outside the armed region, as the interface would.
        if (call == 400) require(engine.launch_scene(1), "scene 2 queued");
        if (call == 900) require(engine.launch_cell(1, 1), "a cell queued");
        if (call == 1200) engine.seek(12345);
        if (call == 1500) require(engine.recompile(edited, 0, &error), "recompile: " + error);
        if (call == 1900) require(engine.stop_launched(0), "a stop queued");
        if (call == 2300) require(engine.launch_scene(0), "scene 1 again");
        while (engine.take_launcher_take(take)) ++takes;

        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        {
            test::AllocationGuard guard;
            engine.process({out_left, out_right});
            total += guard.count();
        }
    }
    engine.set_playing(false);
    std::vector<float> tail_left(block), tail_right(block);
    {
        test::AllocationGuard guard;
        engine.process({tail_left, tail_right});
        total += guard.count();
    }
    while (engine.take_launcher_take(take)) ++takes;

    require_no_allocations(total, "3000 SongEngine::process calls with launched cells");
    // It really played: the arrangement is an empty pattern, so every note
    // heard came from the launcher, which launched at least a dozen loops.
    require(probe::rising_edges(left, 1e-4F) > 12, "the launched cells sounded");
    require(takes >= 4, "arrangement recording handed takes back: " + std::to_string(takes));
    require(probe::peak(tail_left) == 0.0F, "stopping the transport let go of every note");
}
