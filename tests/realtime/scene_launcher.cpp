#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/scene_launcher_engine.hpp"
#include "blokkily/audio/song_engine.hpp"

#include <vector>

namespace {

using namespace blokkily;
using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;
constexpr double rate = 48000.0;

Pattern make_test_pattern(std::int16_t key, Tick length = 1920) {
    Pattern pat(length, 480);
    Trigger tr;
    tr.id = 1;
    tr.start = 0;
    tr.duration = 480;
    tr.musical_data = Note{key, 0.8F, 0.0F};
    (void)pat.add(tr);
    return pat;
}

BLOKKILY_REALTIME_CASE(scene_launcher) {
    Song song;
    song.patterns = {
        {"P0", make_test_pattern(60)},
        {"P1", make_test_pattern(64)}
    };
    song.tracks = {Track{}, Track{}};
    song.clips = {{0, 0, 0, 16}, {1, 1, 0, 16}};

    SceneMatrix matrix;
    matrix.add_scene("Scene 1");
    matrix.add_scene("Scene 2");
    matrix.set_slot(0, 0, SceneSlot{.pattern = 0, .repeats = 2, .quantization = LaunchQuantization::bar, .follow_action = FollowAction::next});
    matrix.set_slot(0, 1, SceneSlot{.pattern = 1, .repeats = 2, .quantization = LaunchQuantization::bar, .follow_action = FollowAction::next});
    matrix.set_slot(1, 0, SceneSlot{.pattern = 1, .repeats = 2, .quantization = LaunchQuantization::bar, .follow_action = FollowAction::again});
    matrix.set_slot(1, 1, SceneSlot{.pattern = 0, .repeats = 2, .quantization = LaunchQuantization::bar, .follow_action = FollowAction::again});
    song.launcher = matrix;

    SongEngine engine;
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "engine prepare: " + error);

    SceneLauncherEngine launcher;
    launcher.prepare(song);
    launcher.set_jam_recording(true);
    engine.set_scene_launcher(&launcher);

    // Launch immediately
    launcher.launch_scene(0, LaunchQuantization::none);
    engine.set_playing(true);

    std::vector<float> left(calls * block, 0.0F);
    std::vector<float> right(calls * block, 0.0F);
    test::AllocationCount total;

    for (std::size_t call = 0; call < calls; ++call) {
        // Trigger live launches mid-stream to stress-test real-time command processing
        if (call == 200) {
            launcher.launch_scene(1, LaunchQuantization::bar);
        } else if (call == 500) {
            launcher.stop_track(0, LaunchQuantization::beat);
        } else if (call == 700) {
            launcher.launch_slot(0, 0, LaunchQuantization::bar);
        }

        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        {
            test::AllocationGuard guard;
            engine.process({out_left, out_right});
            total += guard.count();
        }
    }

    require_no_allocations(total, "1000 SongEngine::process calls with Scene Launcher active");
}

} // namespace
