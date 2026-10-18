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
#include "blokkily/model/scene_launcher.hpp"
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

// Scenario: Removing a lock puts its parameter back
// features/parameter_lock_restore.feature. The CLAP synth (Level 0.25 of its
// own) through the CLAP effect (Gain 0.25 of its own), a held key whose step
// locks both. Each edit is recompiled into the running engine, then heard.
void removed_restores_case() {
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
    const auto id = song.patterns[0].pattern.add(held);
    SongEngine engine;
    build(engine, song);
    engine.set_playing(true);
    const auto heard = [&](const char* what) {
        engine.seek(0);
        const auto left = render(engine, 12000);
        std::cerr << what << ": " << level_at(left, 6000) << '\n';
        return left;
    };
    const auto relock = [&](std::vector<ParameterLock> locks) {
        const auto* step = song.patterns[0].pattern.find(id);
        require(step != nullptr, "the step is there");
        auto edited = *step;
        edited.locks = std::move(locks);
        require(song.patterns[0].pattern.update(id, edited), "the step takes its new locks");
        std::string error;
        require(engine.recompile(song, 0, &error), "the edit recompiles: " + error);
    };
    require(plateau(heard("locked"), 2000, 12000, 0.45F), "locked: 0.9 x 0.5");

    // The Gain's value lock becomes a modulation of +0.5: the value lock's
    // 0.5 goes, and the effect's own 0.25 is what the offset adds to.
    relock({{"Level", 0, 0.9, ParameterLock::Kind::automation, -1},
            {"Gain", 0, 0.5, ParameterLock::Kind::modulation, 0}});
    require(plateau(heard("gain modulated"), 2000, 12000, 0.9F * 0.75F),
            "the Gain is its own 0.25 plus 0.5: 0.9 x 0.75");

    // Every lock removed, in two edits the callback never sees apart: the
    // instrument's Level and the effect's Gain are their own values again.
    relock({{"Level", 0, 0.9, ParameterLock::Kind::automation, -1}});
    relock({});
    require(plateau(heard("unlocked"), 2000, 12000, 0.25F * 0.25F),
            "with no locks the track is 0.25 x 0.25 again, as before it was locked");
    // And it stays there: nothing is owed twice.
    require(plateau(heard("again"), 2000, 12000, 0.25F * 0.25F), "and stays there");
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

// A song whose track is heard only through return 0: a steady signal sent
// pre-fader at 0 dB, the track's own fader all but closed. Return 0 and the
// master each carry the CLAP effect (Gain 0.25 of their own). The track's
// steps lock return 0's Gain to 0.5 at 4000 and the master's to 0.75 at 8000.
Song bus_locked_song() {
    Song song;
    song.patterns = {{"Locks", Pattern(16384, ticks_per_beat)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};
    song.returns = {ReturnBus{}};
    song.returns[0].inserts = {insert(clap_effect())};
    song.master_inserts = {insert(clap_effect())};
    song.tracks[0].mix.gain_db = -120.0;
    song.tracks[0].sends = {{0, 0.0, true}};
    auto on_return = locked_step(4000, 0.5);
    on_return.locks[0].bus_kind = BusKind::ret;
    auto on_master = locked_step(8000, 0.75);
    on_master.locks[0].bus_kind = BusKind::master;
    (void)song.patterns[0].pattern.add(on_return);
    (void)song.patterns[0].pattern.add(on_master);
    return song;
}

double mean(const std::vector<float>& left, std::size_t from, std::size_t to) {
    double sum = 0.0;
    for (std::size_t sample = from; sample < to; ++sample) sum += left[sample];
    return sum / static_cast<double>(to - from);
}

// Scenario: A step lock can move a return's or the master's insert
void bus_locks_case() {
    const auto song = bus_locked_song();
    std::string why;
    require(song.consistent(&why), "the song is valid: " + why);
    float level = 1.0F;
    const auto left = play(song, &level);
    const double own = mean(left, 1000, 3500);
    const double returned = mean(left, 5000, 7500);
    const double mastered = mean(left, 10000, 16000);
    std::cerr << "bus locks: " << own << ", " << returned << ", " << mastered << '\n';
    require(own > 1e-3, "the return is heard through the master");
    require(std::abs(returned / own - 2.0) < 1e-3,
            "return A's Gain locked from 0.25 to 0.5 doubles the level");
    require(std::abs(mastered / own - 6.0) < 1e-3,
            "and the master's from 0.25 to 0.75 triples it again");
}

// Scenario: A launched cell plays its insert locks
// Track 0 carries the CLAP effect, and so does the master; the arrangement
// plays nothing. A cell launched on track 0 locks the track's Gain to 0.5 at
// 4000 and the master's to 0.75 at 8000: each is heard from its step on, as
// the arrangement plays them.
void launched_case() {
    Song song;
    song.patterns = {{"Empty", Pattern(16384, ticks_per_beat)}, {"Locks", Pattern(16384, ticks_per_beat)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};
    song.tracks[0].inserts = {insert(clap_effect())};
    song.master_inserts = {insert(clap_effect())};
    (void)song.patterns[1].pattern.add(locked_step(4000, 0.5));
    auto on_master = locked_step(8000, 0.75);
    on_master.locks[0].bus_kind = BusKind::master;
    (void)song.patterns[1].pattern.add(on_master);
    song.launcher.quantization = LaunchQuantization::none;
    song.launcher.add_scene("A");
    song.launcher.set_slot(0, 0, {.pattern = 1, .quantization = LaunchQuantization::none});
    std::string why;
    require(song.consistent(&why), "the song is valid: " + why);
    SongEngine engine;
    build(engine, song);
    float level = 1.0F;
    engine::TestAccess::set_test_source(engine, 0, dc_source, &level);
    engine.set_playing(true);
    require(engine.launch_scene(0), "the scene launch is queued");
    const auto left = render(engine, 16384);
    const double own = mean(left, 1000, 3500);
    const double track_locked = mean(left, 5000, 7500);
    const double master_locked = mean(left, 10000, 16000);
    std::cerr << "launched locks: " << own << ", " << track_locked << ", " << master_locked << '\n';
    require(own > 1e-3, "the track is heard through both effects");
    require(std::abs(track_locked / own - 2.0) < 1e-3,
            "the launched cell's lock of the track's Gain to 0.5 doubles the level");
    require(std::abs(master_locked / own - 6.0) < 1e-3,
            "and its lock of the master's Gain to 0.75 triples it again");
}

// Scenario: Locks on a return or the master are saved, and go with what they lock
void bus_locks_model_case() {
    Project project;
    project.song = bus_locked_song();
    const auto text = ProjectFile::serialize(project);
    require(text.find(" 0 return 0\n") != std::string::npos && text.find(" 0 master\n") != std::string::npos,
            "each lock names its bus");
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value(), "the project parses back: " + error);
    require(ProjectFile::serialize(*loaded) == text, "the text round-trips byte for byte");
    require(loaded->song.patterns[0].pattern.events()[0].locks[0].bus_kind == BusKind::ret &&
                loaded->song.patterns[0].pattern.events()[1].locks[0].bus_kind == BusKind::master,
            "the locks come back on the return and the master");
    const auto count = [](const Song& song, BusKind kind) {
        std::size_t found = 0;
        for (const auto& trigger : song.patterns[0].pattern.events())
            for (const auto& lock : trigger.locks) found += lock.bus_kind == kind ? 1 : 0;
        return found;
    };
    auto song = project.song;
    require(song.remove_insert(BusKind::master, 0, 0) && count(song, BusKind::master) == 0 &&
                count(song, BusKind::ret) == 1,
            "removing the master's effect takes its lock and leaves the return's");
    song = project.song;
    song.returns.push_back(ReturnBus{});
    song.returns[1].inserts = {insert(clap_effect())};
    auto later = locked_step(12000, 0.3);
    later.locks[0].bus_kind = BusKind::ret;
    later.locks[0].bus = 1;
    (void)song.patterns[0].pattern.add(later);
    require(song.remove_return(0), "return A is removed");
    std::size_t moved = 0;
    for (const auto& trigger : song.patterns[0].pattern.events())
        for (const auto& lock : trigger.locks)
            if (lock.bus_kind == BusKind::ret) moved += lock.bus == 0 ? 1 : 100;
    require(moved == 1, "return A's lock goes and return B's follows it to the first return");
    require(song.consistent(&error), "and the song is still valid: " + error);
    auto broken = project.song;
    auto stray = locked_step(100, 0.3);
    stray.locks[0].bus_kind = BusKind::ret;
    stray.locks[0].bus = 4;
    (void)broken.patterns[0].pattern.add(stray);
    require(!broken.consistent(), "a lock on a return that does not exist is refused");
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
        {"removed_restores", removed_restores_case},
        {"bus_locks", bus_locks_case},
        {"bus_locks_model", bus_locks_model_case},
        {"launched", launched_case},
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
