#include "blokkily/audio/scene_launcher_engine.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/model/scene_launcher.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/project/project.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

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

void quantized_launch_case() {
    constexpr double rate = 48000.0;
    constexpr std::uint32_t block = 256;

    Song song;
    song.patterns = {
        {"P0", make_test_pattern(60)},
        {"P1", make_test_pattern(64)}
    };
    song.tracks = {Track{}, Track{}};
    song.clips = {{0, 0, 0, 8}, {1, 1, 0, 8}};

    // Setup Launcher Matrix
    SceneMatrix matrix;
    matrix.add_scene("Scene 1");
    matrix.add_scene("Scene 2");

    matrix.set_slot(0, 0, SceneSlot{.pattern = 0, .repeats = 0, .quantization = LaunchQuantization::bar});
    matrix.set_slot(0, 1, SceneSlot{.pattern = 1, .repeats = 0, .quantization = LaunchQuantization::bar});
    song.launcher = matrix;

    SongEngine engine;
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);

    SceneLauncherEngine launcher;
    launcher.prepare(song);
    engine.set_scene_launcher(&launcher);

    engine.set_playing(true);

    std::vector<float> left(block), right(block);
    StereoBlock out{left, right};

    // 1. Process 10 blocks (mid-bar). Neither is playing yet.
    for (int i = 0; i < 10; ++i) engine.process(out);
    require(!launcher.is_playing(0) && !launcher.is_playing(1), "tracks not playing before launch");

    // 2. Queue Scene 0 launch with bar quantization
    launcher.launch_scene(0, LaunchQuantization::bar);

    // Process 1 block: now queued
    engine.process(out);
    require(launcher.is_queued(0) && launcher.is_queued(1), "scene 0 is queued for bar boundary");
    require(!launcher.is_playing(0), "not yet playing before bar 1 boundary");

    // Process until past bar 1 (1920 ticks at 120 BPM = 96000 samples = 375 blocks)
    for (int i = 0; i < 400; ++i) engine.process(out);

    require(launcher.is_playing(0), "track 0 started playing after bar boundary");
    require(launcher.is_playing(1), "track 1 started playing after bar boundary");
    require(launcher.active_scene(0) == 0, "track 0 playing scene 0");
    require(launcher.active_scene(1) == 0, "track 1 playing scene 0");
}

void follow_action_case() {
    constexpr double rate = 48000.0;
    constexpr std::uint32_t block = 256;

    Song song;
    song.patterns = {
        {"P0", make_test_pattern(60, 1920)},
        {"P1", make_test_pattern(64, 1920)}
    };
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 8}};

    SceneMatrix matrix;
    matrix.add_scene("Intro");
    matrix.add_scene("Outro");

    // Scene 0: 1 repeat, follow action = next
    matrix.set_slot(0, 0, SceneSlot{
        .pattern = 0,
        .repeats = 1,
        .quantization = LaunchQuantization::none,
        .follow_action = FollowAction::next
    });
    // Scene 1: 1 repeat, follow action = stop
    matrix.set_slot(1, 0, SceneSlot{
        .pattern = 1,
        .repeats = 1,
        .quantization = LaunchQuantization::none,
        .follow_action = FollowAction::stop
    });
    song.launcher = matrix;

    SongEngine engine;
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);

    SceneLauncherEngine launcher;
    launcher.prepare(song);
    engine.set_scene_launcher(&launcher);
    engine.set_playing(true);

    std::vector<float> left(block), right(block);
    StereoBlock out{left, right};

    // Immediate launch of Scene 0
    launcher.launch_slot(0, 0, LaunchQuantization::none);
    engine.process(out);
    require(launcher.is_playing(0), "track 0 playing scene 0");
    require(launcher.active_scene(0) == 0, "active scene is 0");

    // Process through 1 pattern length (1920 ticks = 96000 samples = 375 blocks)
    for (int i = 0; i < 400; ++i) engine.process(out);

    // Follow action 'next' should have automatically triggered Scene 1!
    require(launcher.is_playing(0), "track 0 still playing");
    require(launcher.active_scene(0) == 1, "active scene transitioned to scene 1 via follow action");

    // Process through Scene 1 pattern length
    for (int i = 0; i < 400; ++i) engine.process(out);

    // Follow action 'stop' in Scene 1 should have stopped playback!
    require(!launcher.is_playing(0), "track 0 stopped via follow action stop");
}

void jam_recording_case() {
    constexpr double rate = 48000.0;
    constexpr std::uint32_t block = 256;

    Song song;
    song.patterns = {
        {"P0", make_test_pattern(60, 1920)},
        {"P1", make_test_pattern(72, 1920)}
    };
    song.tracks = {Track{}};

    SceneMatrix matrix;
    matrix.add_scene("Verse");
    matrix.add_scene("Chorus");
    matrix.set_slot(0, 0, SceneSlot{.pattern = 0, .repeats = 0, .quantization = LaunchQuantization::none});
    matrix.set_slot(1, 0, SceneSlot{.pattern = 1, .repeats = 0, .quantization = LaunchQuantization::none});
    song.launcher = matrix;

    SongEngine engine;
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);

    SceneLauncherEngine launcher;
    launcher.prepare(song);
    launcher.set_jam_recording(true);
    engine.set_scene_launcher(&launcher);
    engine.set_playing(true);

    std::vector<float> left(block), right(block);
    StereoBlock out{left, right};

    // 1. Launch Verse
    launcher.launch_slot(0, 0, LaunchQuantization::none);
    for (int i = 0; i < 100; ++i) engine.process(out);

    // 2. Launch Chorus
    launcher.launch_slot(0, 1, LaunchQuantization::none);
    for (int i = 0; i < 100; ++i) engine.process(out);

    // 3. Stop
    launcher.stop_track(0, LaunchQuantization::none);
    engine.process(out);

    // Verify recorded jam records
    auto jams = launcher.take_jam_records();
    require(jams.size() >= 2, "at least 2 jam clips recorded: " + std::to_string(jams.size()));
    require(jams[0].pattern == 0, "first jam clip is Verse (pattern 0)");
    require(jams[1].pattern == 1, "second jam clip is Chorus (pattern 1)");
    require(jams[0].end_tick == jams[1].start_tick, "seamless transition between clips");
    require(jams[1].end_tick > jams[1].start_tick, "chorus has non-zero duration");
}

void project_serialization_case() {
    Project project;
    project.song.patterns = {{"P0", Pattern(1920, 480)}};
    project.song.tracks = {Track{}, Track{}};

    project.song.launcher.add_scene("Verse", 124.0);
    project.song.launcher.add_scene("Chorus", 128.0);

    project.song.launcher.set_slot(0, 0, SceneSlot{
        .pattern = 0,
        .repeats = 2,
        .quantization = LaunchQuantization::bar,
        .follow_action = FollowAction::next
    });
    project.song.launcher.set_slot(1, 1, SceneSlot{
        .pattern = 0,
        .repeats = 4,
        .quantization = LaunchQuantization::two_bars,
        .follow_action = FollowAction::stop
    });

    std::string text = ProjectFile::serialize(project);
    std::string error;
    auto reloaded = ProjectFile::parse(text, &error);
    require(reloaded.has_value(), "parse serialized project: " + error);
    require(reloaded->song.launcher.scenes.size() == 2, "2 scenes preserved");
    require(reloaded->song.launcher.scenes[0].name == "Verse", "scene 0 name Verse");
    require(reloaded->song.launcher.scenes[0].bpm.has_value() && *reloaded->song.launcher.scenes[0].bpm == 124.0, "scene 0 bpm 124");
    require(reloaded->song.launcher.scenes[1].name == "Chorus", "scene 1 name Chorus");

    const auto& s0 = reloaded->song.launcher.slot(0, 0);
    require(s0.has_value(), "slot (0,0) exists");
    require(s0->repeats == 2, "slot (0,0) repeats == 2");
    require(s0->quantization == LaunchQuantization::bar, "slot (0,0) quantization bar");
    require(s0->follow_action == FollowAction::next, "slot (0,0) follow action next");

    const auto& s1 = reloaded->song.launcher.slot(1, 1);
    require(s1.has_value(), "slot (1,1) exists");
    require(s1->repeats == 4, "slot (1,1) repeats == 4");
    require(s1->quantization == LaunchQuantization::two_bars, "slot (1,1) quantization two_bars");
    require(s1->follow_action == FollowAction::stop, "slot (1,1) follow action stop");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: blokkily_scene_launcher_tests <case>\n";
        return 2;
    }
    const std::string name = argv[1];
    try {
        if (name == "quantized_launch") quantized_launch_case();
        else if (name == "follow_actions") follow_action_case();
        else if (name == "jam_recording") jam_recording_case();
        else if (name == "serialization") project_serialization_case();
        else {
            std::cerr << "Unknown case: " << name << "\n";
            return 2;
        }
    } catch (const std::exception& ex) {
        std::cerr << "FAILED: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
