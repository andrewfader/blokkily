// Executable scenarios for features/insert_locks.feature: step locks on a
// track's insert effects, each proved from rendered audio through the
// production SongEngine::process() with the real CLAP effect fixture (built by
// the suite, loaded through its official entry point by the production
// processor factory). The fixture's output is its input, delayed by the 64
// samples it reports as latency, times its Gain parameter (id 0, default
// 0.25) plus any modulation of it, so a lock on Gain reads straight off the
// rendered level. Each case is its own CTest test (insert_locks_<case>).

#include "audio/engine/test_access.hpp"
#include "engine_graph.hpp"
#include "processor_factory.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/project/project.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <numbers>
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
// One tick per sample: 24000 ticks a beat at 120 BPM and 48 kHz.
constexpr Tick ticks_per_beat = 24000;
constexpr std::size_t latency = 64;
const float centre = static_cast<float>(std::cos(std::numbers::pi / 4.0));

PluginSlot clap_effect() {
    return {"CLAP", BLOKKILY_TEST_CLAP_EFFECT_PATH, "dev.blokkily.test.effect", {}};
}
EffectSlot insert(PluginSlot plugin) { return {std::move(plugin), false, {}}; }

// A step at `tick` that sounds nothing the track can hear (it has no
// instrument) and locks the CLAP effect's Gain.
Trigger locked_step(Tick tick, double gain, ParameterLock::Kind kind = ParameterLock::Kind::automation,
                    std::int32_t slot = 0) {
    Trigger trigger;
    trigger.start = tick;
    trigger.duration = 100;
    trigger.musical_data = Note{60, 0.8F};
    trigger.locks = {{"Gain", 0, gain, kind, slot}};
    return trigger;
}

// One track, its clip playing pattern 0, with the CLAP effect as its first
// insert and locks on Gain at 4000 (0.5) and 8000 (0.1).
Song locked_song(ParameterLock::Kind kind = ParameterLock::Kind::automation) {
    Song song;
    song.patterns = {{"Locks", Pattern(16384, ticks_per_beat)}};
    song.tracks = {Track{}};
    song.tracks[0].name = "T0";
    song.clips = {{0, 0, 0, 1}};
    song.tracks[0].inserts = {insert(clap_effect())};
    (void)song.patterns[0].pattern.add(locked_step(4000, 0.5, kind));
    (void)song.patterns[0].pattern.add(locked_step(8000, 0.1, kind));
    return song;
}

void dc_source(void* context, StereoBlock track, std::uint64_t) noexcept {
    const float level = *static_cast<const float*>(context);
    for (auto& sample : track.left) sample += level;
    for (auto& sample : track.right) sample += level;
}

void build(SongEngine& engine, const Song& song) {
    const auto graph = populate_graph(engine, song, {}, {});
    require(graph.error.empty(), "every processor must load: " + graph.error);
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "the song must prepare: " + error);
    load_fresh_state(engine, song, graph);
}

std::vector<float> render(SongEngine& engine, std::size_t frames) {
    std::vector<float> left(frames, 0.0F), right(frames, 0.0F);
    for (std::size_t start = 0; start < frames; start += block) {
        const auto count = std::min<std::size_t>(block, frames - start);
        engine.process({std::span(left).subspan(start, count), std::span(right).subspan(start, count)});
    }
    return left;
}

// The track's level at a sample, undoing the centred strip.
float level_at(const std::vector<float>& left, std::size_t sample) { return left[sample] / centre; }
bool near(float actual, float expected) { return std::abs(actual - expected) <= 1e-5F; }
bool plateau(const std::vector<float>& left, std::size_t from, std::size_t to, float expected) {
    for (std::size_t sample = from; sample < to; ++sample)
        if (!near(level_at(left, sample), expected)) return false;
    return true;
}
// The first sample from `from` on at `expected`, or the end.
std::size_t first_at(const std::vector<float>& left, std::size_t from, float expected) {
    for (std::size_t sample = from; sample < left.size(); ++sample)
        if (near(level_at(left, sample), expected)) return sample;
    return left.size();
}

// The song played once from its start with a steady 1.0 into the track.
std::vector<float> play(const Song& song, float* level) {
    SongEngine engine;
    build(engine, song);
    engine::TestAccess::set_test_source(engine, 0, dc_source, level);
    engine.set_playing(true);
    return render(engine, 16384);
}

// Scenario: A step lock on an insert effect is heard from its step on
void heard_case() {
    float level = 1.0F;
    const auto left = play(locked_song(), &level);
    require(plateau(left, latency, 3990, 0.25F),
            "before the first lock the effect plays its own Gain of 0.25, read " +
                std::to_string(level_at(left, 2000)));
    const auto raised = first_at(left, 3990, 0.5F);
    require(raised >= 4000 && raised <= 4000 + latency,
            "the lock to 0.5 at step 4000 is heard from there, at " + std::to_string(raised));
    require(plateau(left, 4100, 7990, 0.5F), "and holds until the next lock");
    const auto lowered = first_at(left, 7990, 0.1F);
    require(lowered - raised == 4000, "the lock at 8000 lands 4000 samples after it, not " +
                                          std::to_string(lowered - raised));
    require(plateau(left, 8100, 16384, 0.1F), "and holds to the end of the song");
}

// Scenario: A modulation lock on an insert offsets its parameter
void modulation_case() {
    float level = 1.0F;
    const auto left = play(locked_song(ParameterLock::Kind::modulation), &level);
    require(plateau(left, latency, 3990, 0.25F), "before the lock the Gain is its own 0.25");
    require(plateau(left, 4100, 7990, 0.75F),
            "a modulation lock of 0.5 offsets it to 0.75, read " +
                std::to_string(level_at(left, 6000)));
    require(plateau(left, 8100, 16384, 0.35F), "and one of 0.1 to 0.35");
}

// Scenario: A lock on an insert leaves the instrument alone
void instrument_untouched_case() {
    // Two locks on one step, both on parameter 0: the instrument's Level
    // and the effect's Gain. The CLAP synth sounds its Level while a key is
    // down; the effect scales that by its Gain.
    Song song;
    song.patterns = {{"Locks", Pattern(16384, ticks_per_beat)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};
    song.tracks[0].instrument = {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", {}};
    song.tracks[0].inserts = {insert(clap_effect())};
    Trigger held;
    held.start = 0;
    held.duration = 16000;
    held.musical_data = Note{60, 1.0F};
    held.locks = {{"Level", 0, 0.9, ParameterLock::Kind::automation, -1},
                  {"Gain", 0, 0.5, ParameterLock::Kind::automation, 0}};
    (void)song.patterns[0].pattern.add(held);
    SongEngine engine;
    build(engine, song);
    engine.set_playing(true);
    const auto left = render(engine, 12000);
    require(plateau(left, 2000, 12000, 0.45F),
            "the instrument plays its lock of 0.9 and the effect its own of 0.5: 0.45, read " +
                std::to_string(level_at(left, 6000)));
}

// Scenario: Insert locks are bounced as they are heard
void bounce_case() {
    const auto song = locked_song();
    float level = 1.0F;
    SongEngine engine;
    build(engine, song);
    engine::TestAccess::set_test_source(engine, 0, dc_source, &level);
    const auto file = std::filesystem::path(BLOKKILY_TEST_ARTIFACTS) / "insert-locks-bounce.wav";
    std::filesystem::create_directories(file.parent_path());
    std::string error;
    const auto report = bounce_song(engine, file, WaveFormat::float32, 0, &error);
    require(report.has_value(), "the bounce is written: " + error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value() && wave->channels == 2, "the bounce reads back: " + error);
    const auto played = play(song, &level);
    // The file starts where the listener does, the latency trimmed off.
    bool same = true;
    for (std::size_t frame = 0; frame + latency < played.size() && frame < wave->frames && same;
         ++frame)
        same = wave->interleaved[2 * frame] == played[frame + latency];
    require(same, "the bounce is what playback rendered, sample for sample");
    const auto at = [&](std::size_t frame) { return wave->interleaved[2 * frame] / centre; };
    require(near(at(2000), 0.25F) && near(at(6000), 0.5F) && near(at(12000), 0.1F),
            "and in the file the Gain is 0.25, then 0.5, then 0.1");
}

// Scenario: Insert locks survive a save and a load
void project_case() {
    Project project;
    project.song = locked_song(ParameterLock::Kind::modulation);
    const auto text = ProjectFile::serialize(project);
    require(text.find("lockslot 0 ") != std::string::npos, "each insert lock names its slot");
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value(), "the project parses back: " + error);
    require(ProjectFile::serialize(*loaded) == text, "the text round-trips byte for byte");
    const auto& events = loaded->song.patterns[0].pattern.events();
    require(events.size() == 2 && events[0].locks.size() == 1 && events[0].locks[0].slot == 0 &&
                events[0].locks[0].kind == ParameterLock::Kind::modulation,
            "the locks come back on the insert, as modulation");
    float level = 1.0F;
    require(play(project.song, &level) == play(loaded->song, &level),
            "the loaded song renders exactly what the saved one did");
    // A file with no insert locks writes no lockslot record.
    Project plain;
    plain.song = locked_song();
    for (auto& slot : plain.song.patterns)
        for (auto trigger : std::vector<Trigger>(slot.pattern.events().begin(),
                                                 slot.pattern.events().end())) {
            trigger.locks[0].slot = -1;
            (void)slot.pattern.update(trigger.id, trigger);
        }
    require(ProjectFile::serialize(plain).find("lockslot") == std::string::npos,
            "a song with instrument locks only has no lockslot record");
}

// Scenario: Removing an insert takes its locks with it
void remove_insert_case() {
    auto song = locked_song();
    song.tracks[0].inserts.push_back(insert(clap_effect()));
    // A lock on the second effect too, at 12000.
    (void)song.patterns[0].pattern.add(locked_step(12000, 0.75, ParameterLock::Kind::automation, 1));
    require(song.remove_insert(BusKind::track, 0, 0), "the first effect is removed");
    std::size_t on_first = 0;
    std::size_t locks = 0;
    for (const auto& trigger : song.patterns[0].pattern.events())
        for (const auto& lock : trigger.locks) {
            ++locks;
            if (lock.slot == 0) ++on_first;
        }
    require(locks == 1 && on_first == 1,
            "the removed effect's two locks go; the second effect's follows it to slot 0");
    float level = 1.0F;
    const auto left = play(song, &level);
    require(plateau(left, latency, 11990, 0.25F), "the remaining effect plays its own 0.25 first");
    require(plateau(left, 12100, 16384, 0.75F), "and its lock of 0.75 from step 12000");
}

} // namespace

int main(int argc, char** argv) {
    const std::map<std::string, void (*)()> cases{
        {"heard", heard_case},
        {"modulation", modulation_case},
        {"instrument_untouched", instrument_untouched_case},
        {"bounce", bounce_case},
        {"project", project_case},
        {"remove_insert", remove_insert_case},
    };
    if (argc != 2 || !cases.contains(argv[1])) {
        std::cerr << "usage: blokkily_insert_locks_tests <case>\n";
        return 2;
    }
    try {
        cases.at(argv[1])();
    } catch (const std::exception& failure) {
        std::cerr << argv[1] << ": " << failure.what() << '\n';
        return 1;
    }
    std::cout << argv[1] << ": ok\n";
    return 0;
}
