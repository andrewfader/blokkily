// Pattern swing: even sixteenths are delayed when they play, proved from
// Scheduler::render and from what SongEngine::process() rendered with the
// real CLAP fixture. An export is the same mix, read back from the file.
//
// Run with a case name; each case is its own CTest test (swing_<case>).
// features/pattern_swing.feature names the scenario each case executes.

#include "support/audio_probe.hpp"
#include "support/process_id.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/project/project.hpp"
#include "blokkily/sequencer/scheduler.hpp"

#include <algorithm>
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
// 120 BPM, 480 ticks a beat: 50 samples a tick, 120 ticks a sixteenth.
constexpr Tick sixteenth = 120;
constexpr std::size_t sixteenth_samples = 6000;

Trigger held(Tick start, int key) {
    Trigger trigger;
    trigger.start = start;
    trigger.duration = 60;
    trigger.musical_data = Note{static_cast<std::int16_t>(key), 1.0F, 0.0F};
    return trigger;
}

Pattern two_sixteenths(int swing) {
    Pattern pattern(1920, 480);
    pattern.set_swing(swing);
    (void)pattern.add(held(0, 60));
    (void)pattern.add(held(sixteenth, 64));
    return pattern;
}

Song song_of(Pattern pattern) {
    Song song;
    song.patterns = {{"SWING", std::move(pattern)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};
    return song;
}

std::unique_ptr<PluginInstance> clap() {
    std::string error;
    auto instance = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
    require(instance != nullptr, "the CLAP fixture must load: " + error);
    return instance;
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

void prepare(SongEngine& engine, Song& song) {
    engine.set_instrument(0, clap());
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
}

std::filesystem::path scratch_file(const std::string& name) {
    return std::filesystem::temp_directory_path() /
           ("blokkily-swing-" + std::to_string(blokkily::test::process_id()) + "-" + name);
}

// features/pattern_swing.feature: The scheduler delays even sixteenths
void scheduler_delays_offbeats_case() {
    const auto notes = Scheduler{}.render(two_sixteenths(100), 1, 0).notes;
    require(notes.size() == 2, "both notes are scheduled");
    require(notes[0].start == 0, "the downbeat stays put");
    require(notes[1].start == sixteenth + sixteenth / 2,
            "100% swing delays the even sixteenth by half a step: " +
                std::to_string(notes[1].start));
    require(two_sixteenths(100).swing_delay(0) == 0, "odd sixteenths are not delayed");
    require(two_sixteenths(67).swing_delay(sixteenth) == 40, "67% is a 40-tick delay");
}

// features/pattern_swing.feature: With swing at 0% the grid plays straight
void off_is_straight_case() {
    const auto notes = Scheduler{}.render(two_sixteenths(0), 1, 0).notes;
    require(notes.size() == 2, "both notes are scheduled");
    require(notes[0].start == 0 && notes[1].start == sixteenth, "the grid is straight");
}

// features/pattern_swing.feature: Playback hears the delayed sixteenth
void heard_case() {
    auto swung = song_of(two_sixteenths(100));
    SongEngine engine;
    prepare(engine, swung);
    engine.set_playing(true);
    const auto heard = render(engine, 3 * sixteenth_samples);
    engine.set_playing(false);

    require(sounding(std::span<const float>{heard.left}.subspan(0, 400)),
            "the downbeat sounds at the start");
    require(silent(std::span<const float>{heard.left}.subspan(sixteenth_samples, 400)),
            "the even sixteenth is silent at its straight time");
    require(sounding(std::span<const float>{heard.left}.subspan(
                sixteenth_samples + sixteenth_samples / 2, 400)),
            "and sounds half a sixteenth later");

    std::string error;
    const auto file = scratch_file("swing-bounce.wav");
    require(bounce_song(engine, file, WaveFormat::float32, 0, &error).has_value(),
            "the bounce: " + error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value(), "the bounce reads back: " + error);
    require(wave->channels == 2 && wave->frames >= 3 * sixteenth_samples, "a stereo file of the bar");
    std::vector<float> left(static_cast<std::size_t>(std::min<std::uint64_t>(
        wave->frames, 3 * sixteenth_samples)));
    for (std::size_t frame = 0; frame < left.size(); ++frame)
        left[frame] = wave->interleaved[frame * 2];
    require(sounding(std::span<const float>{left}.subspan(0, 400)), "the file starts on the downbeat");
    require(silent(std::span<const float>{left}.subspan(sixteenth_samples, 400)),
            "the file is silent at the straight even sixteenth");
    require(sounding(std::span<const float>{left}.subspan(
                sixteenth_samples + sixteenth_samples / 2, 400)),
            "and has the delayed sixteenth");
    std::filesystem::remove(file);
}

// features/pattern_swing.feature: Swing is saved with the pattern
void project_case() {
    Project project;
    require(ProjectFile::serialize(project).find("swing") == std::string::npos,
            "a straight project saves no swing record");
    project.song.patterns[0].pattern.set_swing(67);
    const auto text = ProjectFile::serialize(project);
    require(text.find("swing 0 67\n") != std::string::npos, "the swing is written: " + text);
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value(), "the project loads: " + error);
    require(loaded->song.patterns[0].pattern.swing() == 67, "the swing loads as saved");
    require(ProjectFile::serialize(*loaded) == text, "and save byte for byte the same");

    const auto base = ProjectFile::serialize(Project{});
    const auto defaults = ProjectFile::parse(base, &error);
    require(defaults.has_value() && defaults->song.patterns[0].pattern.swing() == 0,
            "a file without the record loads straight");
}

// features/pattern_swing.feature: A malformed swing record is refused
void malformed_case() {
    const auto base = ProjectFile::serialize(Project{});
    std::string error;
    for (const char* bad : {"swing 0 101", "swing 0 -1", "swing maybe 50", "swing 0", "swing 9 50"}) {
        error.clear();
        require(!ProjectFile::parse(base + bad + "\n", &error).has_value() && !error.empty(),
                std::string("a malformed record is refused: ") + bad);
    }
    error.clear();
    require(!ProjectFile::parse(base + "swing 0 50\nswing 0 25\n", &error).has_value(),
            "two swing records for one pattern are refused");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "pass a case name");
        const std::string name = argv[1];
        if (name == "scheduler_delays_offbeats") scheduler_delays_offbeats_case();
        else if (name == "off_is_straight") off_is_straight_case();
        else if (name == "heard") heard_case();
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
