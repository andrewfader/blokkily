// The arrangement loop: playback wraps at OUT back to IN, proved from what
// SongEngine::process() rendered with the real CLAP fixture. An export is
// the whole arrangement, read back from the file it wrote.
//
// Run with a case name; each case is its own CTest test (loop_<case>).
// features/arrangement_loop.feature names the scenario each case executes.

#include "support/audio_probe.hpp"
#include "support/process_id.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/project/project.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 256;
// 120 BPM, 480 ticks a beat: 50 samples a tick, 96000 samples a 4/4 bar.
constexpr std::size_t bar_samples = 96000;
constexpr Tick bar_ticks = 1920;

std::unique_ptr<PluginInstance> clap() {
    std::string error;
    auto instance = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
    require(instance != nullptr, "the CLAP fixture must load: " + error);
    return instance;
}

Trigger held(Tick start, int key) {
    Trigger trigger;
    trigger.start = start;
    trigger.duration = 240;
    trigger.musical_data = Note{static_cast<std::int16_t>(key), 1.0F, 0.0F};
    return trigger;
}

// Bar 1 has HIT, bar 3 has HOOK. Each pattern is one bar; HOOK sits at bar 3
// (tick 3840). The song lasts four bars so a loop of bar 3 has room after it.
Song loop_song() {
    Pattern hit(bar_ticks, 480);
    (void)hit.add(held(0, 60));
    Pattern hook(bar_ticks, 480);
    (void)hook.add(held(0, 64));
    Song song;
    song.patterns = {{"HIT", std::move(hit)}, {"HOOK", std::move(hook)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}, {0, 1, 2 * bar_ticks, 1}};
    return song;
}

struct Stereo {
    std::vector<float> left;
    std::vector<float> right;
};

Stereo render(SongEngine& engine, std::size_t frames) {
    Stereo out{std::vector<float>(frames, 0.0F), std::vector<float>(frames, 0.0F)};
    for (std::size_t at = 0; at < frames; at += block) {
        const auto count = std::min<std::size_t>(block, frames - at);
        engine.process({std::span{out.left}.subspan(at, count),
                        std::span{out.right}.subspan(at, count)});
    }
    return out;
}

bool sounding(std::span<const float> samples) { return probe::peak(samples) > 0.05F; }

bool silent(std::span<const float> samples) { return probe::peak(samples) < 1e-4F; }

std::span<const float> window(const std::vector<float>& samples, std::size_t at,
                              std::size_t length = 4000) {
    return std::span<const float>{samples}.subspan(at, length);
}

std::filesystem::path scratch_file(const std::string& name) {
    return std::filesystem::temp_directory_path() /
           ("blokkily-loop-" + std::to_string(blokkily::test::process_id()) + "-" + name);
}

void prepare(SongEngine& engine, Song& song) {
    engine.set_instrument(0, clap());
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
}

// features/arrangement_loop.feature: Playback wraps at OUT back to IN
void wraps_playback_case() {
    auto song = loop_song();
    song.loop = {true, 2 * bar_ticks, 3 * bar_ticks};
    SongEngine engine;
    prepare(engine, song);
    require(engine.loop_enabled(), "apply_mix turns the loop on");
    require(engine.song_samples() >= 3 * bar_samples, "the song reaches bar 4");

    engine.seek(2 * bar_samples);
    engine.set_playing(true);
    const auto heard = render(engine, 2 * bar_samples);
    engine.set_playing(false);

    require(sounding(window(heard.left, 0)), "bar 3's note sounds on the first pass");
    require(silent(window(heard.left, 20000)), "and is silent later in that bar");
    require(sounding(window(heard.left, bar_samples)), "the same note sounds again after the wrap");
    require(silent(window(heard.left, bar_samples + 20000)), "the wrap is still only bar 3");
    // Bar 1's note never plays: the playhead stayed in [bar 3, bar 4).
    require(engine.sample_position() >= 2 * bar_samples &&
                engine.sample_position() < 3 * bar_samples,
            "the playhead stays inside the loop");
}

// features/arrangement_loop.feature: LOOP turned on after the song has come
// round keeps the playhead where it is. Regression: the playhead counted on
// past the song's end while the loop was off, and turning it on folded that
// count into the loop, jumping to another bar.
void turned_on_after_a_lap_case() {
    auto song = loop_song();
    SongEngine engine;
    prepare(engine, song);
    const auto length = engine.song_samples();
    require(length > 2 * bar_samples, "the song is longer than the loop");
    engine.set_playing(true);
    // Round the song once and half a bar into its first bar.
    (void)render(engine, static_cast<std::size_t>(length + bar_samples / 2));
    song.loop = {true, bar_ticks, 3 * bar_ticks};
    engine.apply_mix(song);
    (void)render(engine, block);
    engine.set_playing(false);
    const auto at = engine.sample_position();
    std::cout << "playhead at " << at << " of a " << bar_samples << "-sample bar\n";
    require(at >= bar_samples / 2 && at < bar_samples,
            "the playhead stays in the first bar, where the song had come round to");
}

// features/arrangement_loop.feature: With LOOP off the song plays through
void off_plays_the_song_case() {
    auto song = loop_song();
    SongEngine engine;
    prepare(engine, song);
    require(!engine.loop_enabled(), "the loop is off by default");
    engine.set_playing(true);
    const auto heard = render(engine, 3 * bar_samples);
    engine.set_playing(false);
    require(sounding(window(heard.left, 0)), "bar 1's note sounds at the start");
    require(silent(window(heard.left, 2 * bar_samples - 4000, 2000)),
            "bar 2 is empty");
    require(sounding(window(heard.left, 2 * bar_samples)), "bar 3's note sounds after it");
}

// features/arrangement_loop.feature: A mute still reaches the mix while looping
void mixer_live_case() {
    auto song = loop_song();
    song.loop = {true, 2 * bar_ticks, 3 * bar_ticks};
    SongEngine engine;
    prepare(engine, song);
    engine.seek(2 * bar_samples);
    engine.set_playing(true);
    const auto open = render(engine, bar_samples / 8);
    require(sounding(window(open.left, 0)), "the looped note is heard");

    song.tracks[0].mix.mute = true;
    engine.apply_mix(song);
    engine.seek(2 * bar_samples);
    const auto muted = render(engine, bar_samples / 8);
    require(silent(std::span<const float>{muted.left}), "mute silences the looped note");

    song.tracks[0].mix.mute = false;
    engine.apply_mix(song);
    // Seek back onto the note so the remainder of the bar is not empty.
    engine.seek(2 * bar_samples);
    const auto restored = render(engine, bar_samples / 4);
    engine.set_playing(false);
    require(sounding(window(restored.left, 0)), "unmuting brings the looped note back");
    require(engine.loop_enabled(), "mute did not rebuild or drop the loop");
}

// features/arrangement_loop.feature: An export is the whole arrangement
void bounce_is_the_arrangement_case() {
    auto song = loop_song();
    song.loop = {true, 2 * bar_ticks, 3 * bar_ticks};
    SongEngine engine;
    prepare(engine, song);
    require(engine.loop_enabled(), "the session is looping");
    std::string error;
    const auto file = scratch_file("loop-bounce.wav");
    require(bounce_song(engine, file, WaveFormat::float32, 0, &error).has_value(),
            "the bounce: " + error);
    require(engine.loop_enabled(), "the bounce put the session's loop back on");

    const auto wave = read_wave(file, &error);
    require(wave.has_value(), "the bounce reads back: " + error);
    require(wave->channels == 2 && wave->frames >= 3 * bar_samples, "a stereo file of the song");
    std::vector<float> left(static_cast<std::size_t>(wave->frames), 0.0F);
    for (std::uint64_t frame = 0; frame < wave->frames; ++frame)
        left[static_cast<std::size_t>(frame)] = wave->interleaved[frame * 2];

    require(sounding(window(left, 0)), "the file starts with bar 1's note");
    require(silent(window(left, 2 * bar_samples - 4000, 2000)), "bar 2 is empty in the file");
    require(sounding(window(left, 2 * bar_samples)), "then bar 3's note, not a second bar 1");

    auto off = loop_song();
    SongEngine plain;
    prepare(plain, off);
    const auto plain_file = scratch_file("plain-bounce.wav");
    require(bounce_song(plain, plain_file, WaveFormat::float32, 0, &error).has_value(),
            "the loop-off bounce: " + error);
    const auto plain_wave = read_wave(plain_file, &error);
    require(plain_wave.has_value(), "the loop-off bounce reads back: " + error);
    require(plain_wave->frames == wave->frames, "both files last the song");
    require(plain_wave->interleaved == wave->interleaved,
            "the looped session's export is the same mix as with the loop off");
    std::filesystem::remove(file);
    std::filesystem::remove(plain_file);
}

// features/arrangement_loop.feature: The loop is saved with the project
void project_case() {
    Project project;
    require(ProjectFile::serialize(project).find("loop") == std::string::npos,
            "a project that never looped saves no loop record");
    project.song.loop = {true, 1920, 3840};
    const auto text = ProjectFile::serialize(project);
    require(text.find("loop on 1920 3840\n") != std::string::npos, "the loop is written: " + text);
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value(), "the project loads: " + error);
    require(loaded->song.loop == project.song.loop, "the loop loads as saved");
    require(ProjectFile::serialize(*loaded) == text, "and save byte for byte the same");

    Project parked;
    parked.song.loop = {false, 1920, 3840};
    const auto parked_text = ProjectFile::serialize(parked);
    require(parked_text.find("loop off 1920 3840\n") != std::string::npos,
            "a region with the loop off is still stored");
    const auto parked_loaded = ProjectFile::parse(parked_text, &error);
    require(parked_loaded.has_value() && parked_loaded->song.loop == parked.song.loop,
            "the parked region loads");

    const auto base = ProjectFile::serialize(Project{});
    const auto defaults = ProjectFile::parse(base, &error);
    require(defaults.has_value() && defaults->song.loop == ArrangementLoop{},
            "a file without the record loads with the loop off");
}

// features/arrangement_loop.feature: A malformed loop record is refused
void malformed_case() {
    const auto base = ProjectFile::serialize(Project{});
    std::string error;
    for (const char* bad : {"loop maybe 0 1920", "loop on -1 1920", "loop on 1920 1920",
                            "loop on 3840 1920", "loop on 0", "loop on"}) {
        error.clear();
        require(!ProjectFile::parse(base + bad + "\n", &error).has_value() && !error.empty(),
                std::string("a malformed record is refused: ") + bad);
    }
    error.clear();
    require(!ProjectFile::parse(base + "loop on 0 1920\nloop off 0 1920\n", &error).has_value(),
            "two loop records are refused");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "pass a case name");
        const std::string name = argv[1];
        if (name == "wraps_playback") wraps_playback_case();
        else if (name == "off_plays_the_song") off_plays_the_song_case();
        else if (name == "turned_on_after_a_lap") turned_on_after_a_lap_case();
        else if (name == "mixer_live") mixer_live_case();
        else if (name == "bounce_is_the_arrangement") bounce_is_the_arrangement_case();
        else if (name == "project") project_case();
        else if (name == "malformed") malformed_case();
        else throw std::runtime_error("unknown case " + name);
        std::cout << name << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
