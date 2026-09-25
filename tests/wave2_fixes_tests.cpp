// Regression cases for the confirmed Wave 2 review findings, each proved from
// what the production SongEngine::process() rendered (or wrote and read back)
// with the real CLAP and VST3 fixtures and the built-in effects, created
// through the production processor factory.
//
// Run with a case name; each case is its own CTest test (wave2_<case>).
// The scenarios they execute are named in features/effects.feature,
// features/record_everything.feature and features/audio_reliability.feature.

#include "audio/engine/test_access.hpp"
#include "engine_graph.hpp"
#include "processor_factory.hpp"
#include "support/audio_probe.hpp"

#include "blokkily/audio/audio_asset.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/effects/builtin.hpp"
#include "blokkily/midi/input_routes.hpp"
#include "blokkily/midi/midi_input.hpp"
#include "blokkily/sequencer/take.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace blokkily;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 256;

PluginSlot clap_synth() { return {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", {}}; }
PluginSlot clap_effect() {
    return {"CLAP", BLOKKILY_TEST_CLAP_EFFECT_PATH, "dev.blokkily.test.effect", {}};
}
PluginSlot vst3_effect() { return {"VST3", BLOKKILY_TEST_VST3_EFFECT_PATH, "", {}}; }
PluginSlot builtin(const char* identifier) {
    return {std::string(builtin_effect_format), "", identifier, {}};
}
EffectSlot insert(PluginSlot plugin) { return {std::move(plugin), false, {}}; }

Trigger note(Tick start, Tick duration, int key) {
    Trigger trigger;
    trigger.start = start;
    trigger.duration = duration;
    trigger.musical_data = Note{static_cast<std::int16_t>(key), 1.0F, 0.0F};
    return trigger;
}

// Puts every processor the song names into `engine` through the production
// factory, prepares it, and loads the saved state of what was created.
void build(SongEngine& engine, const Song& song, std::uint32_t maximum = block) {
    const auto graph = populate_graph(engine, song, {}, {});
    require(graph.error.empty(), "every processor must load: " + graph.error);
    std::string error;
    require(engine.prepare(song, rate, maximum, 0, &error), "the song must prepare: " + error);
    load_fresh_state(engine, song, graph);
}

struct Stereo {
    std::vector<float> left, right;
};
// The production callback, `frames` long, in blocks of `size`.
Stereo render(SongEngine& engine, std::size_t frames, std::size_t size = block) {
    Stereo out{std::vector<float>(frames, 0.0F), std::vector<float>(frames, 0.0F)};
    for (std::size_t start = 0; start < frames; start += size) {
        const auto count = std::min<std::size_t>(size, frames - start);
        engine.process({std::span(out.left).subspan(start, count),
                        std::span(out.right).subspan(start, count)});
    }
    return out;
}

// The first frame louder than `threshold`, or the size when there is none.
std::size_t onset(const std::vector<float>& samples, float threshold = 0.01F) {
    for (std::size_t frame = 0; frame < samples.size(); ++frame)
        if (std::abs(samples[frame]) > threshold) return frame;
    return samples.size();
}

std::filesystem::path artifact(const char* name) {
    const auto file = std::filesystem::path(BLOKKILY_TEST_ARTIFACTS) / name;
    std::filesystem::create_directories(file.parent_path());
    return file;
}

WaveData bounce_to(SongEngine& engine, const char* name) {
    std::string error;
    const auto file = artifact(name);
    const auto report = bounce_song(engine, file, WaveFormat::float32, 0, &error);
    require(report.has_value(), std::string("the bounce is written: ") + error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value() && wave->channels == 2, "the bounce reads back: " + error);
    return *wave;
}

// A song whose every stage holds signal after it has played for a while: the
// CLAP synth through the CLAP effect (64 samples of line), the VST3 effect
// (32) and the delay; a send into a reverb on a return; a compressor on the
// master.
Song ringing_song() {
    Pattern pattern(24000, 24000);   // one tick per sample at 120 BPM
    (void)pattern.add(note(0, 3000, 60));
    (void)pattern.add(note(6000, 2000, 64));
    Song song;
    song.patterns = {{"Ring", std::move(pattern)}};
    song.tracks = {Track{}};
    song.tracks[0].name = "Ringing";
    song.tracks[0].instrument = clap_synth();
    song.tracks[0].inserts = {insert(clap_effect()), insert(vst3_effect()),
                              insert(builtin("delay"))};
    song.returns = {ReturnBus{}};
    song.returns[0].inserts = {insert(builtin("reverb"))};
    song.tracks[0].sends = {{0, -6.0, false}};
    song.master_inserts = {insert(builtin("compressor"))};
    song.clips = {{0, 0, 0, 1}};
    return song;
}

// Where the two files first differ, and in how many samples.
struct Difference {
    std::size_t count = 0;
    std::size_t first = 0;
};
Difference compare(const std::vector<float>& a, const std::vector<float>& b) {
    Difference difference{0, a.size()};
    for (std::size_t index = 0; index < std::min(a.size(), b.size()); ++index)
        if (a[index] != b[index]) {
            if (difference.count == 0) difference.first = index;
            ++difference.count;
        }
    return difference;
}

// Scenario: An export after playback renders the song from silence
void bounce_after_playback_case() {
    const auto song = ringing_song();
    SongEngine fresh;
    build(fresh, song);
    const auto expected = bounce_to(fresh, "wave2-bounce-fresh.wav");

    // The same song played for 7000 frames first: echoes, a reverb tail and
    // the compensation lines are full when the export starts.
    SongEngine played;
    build(played, song);
    played.set_playing(true);
    const auto heard = render(played, 7000);
    require(probe::rms(heard.left) > 1e-3, "the song was heard before the export");
    const auto exported = bounce_to(played, "wave2-bounce-after-playback.wav");

    require(exported.frames == expected.frames, "both files are the same length");
    require(probe::peak(expected.interleaved) > 0.01F, "the export is not silent");
    const auto difference = compare(exported.interleaved, expected.interleaved);
    require(difference.count == 0,
            "the export after playback is the fresh export sample for sample; " +
                std::to_string(difference.count) + " samples differ, the first at frame " +
                std::to_string(difference.first / 2));
}

// Scenario: Playback after an export resumes from silence where it was
void bounce_resumes_clean_case() {
    const auto song = ringing_song();
    constexpr std::uint64_t resume_at = 5120;
    SongEngine engine;
    build(engine, song);
    engine.set_playing(true);
    (void)render(engine, resume_at);
    require(engine.sample_position() == resume_at, "the song played to the resume point");
    (void)bounce_to(engine, "wave2-bounce-resume.wav");
    require(engine.is_playing() && engine.sample_position() == resume_at,
            "the export puts the transport back where it was, playing");

    // What plays on is what a freshly prepared engine plays from there: the
    // export's own tail is not left ringing into the session.
    SongEngine reference;
    build(reference, song);
    reference.seek(resume_at);
    reference.set_playing(true);
    const auto resumed = render(engine, 12000);
    const auto wanted = render(reference, 12000);
    require(probe::rms(wanted.left) > 1e-3, "the song plays on");
    const auto left = compare(resumed.left, wanted.left);
    const auto right = compare(resumed.right, wanted.right);
    require(left.count == 0 && right.count == 0,
            "playback after the export matches a fresh engine from the same place; " +
                std::to_string(left.count + right.count) + " samples differ, the first at " +
                std::to_string(std::min(left.first, right.first)));
}

// --- The arrangement handoff -------------------------------------------------

// A song of one track holding one audio clip, the length of the song, of a
// steady `level`: every rendered sample says which arrangement played it.
struct LevelSong {
    Song song;
    AudioAssets assets;
};
constexpr std::uint64_t clip_frames = 96000;   // one bar of 4/4 at 120 BPM
LevelSong level_song(float level) {
    LevelSong made;
    made.song.patterns = {{"Empty", Pattern(1920, 480)}};
    made.song.tracks = {Track{}};
    made.song.clips = {{0, 0, 0, 1}};
    made.song.audio_files = {{"level.wav", clip_frames, 48000, 1}};
    made.song.audio_clips = {{1, 0, 0, 0, 0, clip_frames, 0.0, 0, 0, {}}};
    auto asset = std::make_shared<AudioAsset>();
    asset->rate = 48000;
    asset->frames = clip_frames;
    asset->left.assign(clip_frames, level);
    made.assets = {asset};
    return made;
}

// The level a block was played at, if every sample of it agrees; -1 if not.
float block_level(const std::vector<float>& samples) {
    for (const float sample : samples)
        if (sample != samples.front()) return -1.0F;
    return samples.front();
}

struct ProbeRecompile {
    SongEngine* engine = nullptr;
    const LevelSong* next = nullptr;
    int fired = 0;
    bool compiled = false;
};

// Scenario: A recompile made while the callback takes an arrangement never
// overwrites the one it is taking
void handoff_probe_case() {
    const auto first = level_song(0.125F);
    const auto queued = level_song(0.25F);
    const auto later = level_song(0.5F);
    SongEngine engine;
    std::string error;
    require(engine.prepare(first.song, rate, block, 0, &error, first.assets), "prepare: " + error);
    engine.set_playing(true);
    const float gain = block_level(render(engine, block).left) / 0.125F;
    require(gain > 0.5F, "the first arrangement is heard");

    // A recompile is queued; while the callback takes it, another recompile
    // lands, exactly between the callback's claim and its report of what it
    // now plays.
    require(engine.recompile(queued.song, 0, &error, queued.assets), "recompile: " + error);
    ProbeRecompile probe{&engine, &later};
    engine::TestAccess::set_handoff_probe(
        engine,
        [](void* context) {
            auto& state = *static_cast<ProbeRecompile*>(context);
            if (state.fired++ > 0) return;
            state.compiled = state.engine->recompile(state.next->song, 0, nullptr,
                                                     state.next->assets);
        },
        &probe);
    const float taken = block_level(render(engine, block).left);
    const float next = block_level(render(engine, block).left);
    engine::TestAccess::set_handoff_probe(engine, nullptr, nullptr);
    require(probe.fired == 2 && probe.compiled, "the probe recompiled inside the handoff");
    require(taken == 0.25F * gain,
            "the block that took the queued arrangement plays it (0.25), not the one "
            "compiled over it mid-handoff: heard " + std::to_string(taken / gain));
    require(next == 0.5F * gain, "the next block plays the later arrangement: heard " +
                                     std::to_string(next / gain));
}

// Scenario: Recompiling from the control thread while the callback runs
// plays every arrangement whole
void handoff_stress_case() {
    constexpr int generations = 3000;
    const auto level_of = [](int generation) {
        return static_cast<float>(generation) / 8192.0F;
    };
    auto first = level_song(level_of(1));
    SongEngine engine;
    std::string error;
    require(engine.prepare(first.song, rate, 64, 0, &error, first.assets), "prepare: " + error);
    engine.set_playing(true);
    const float gain = block_level(render(engine, 64, 64).left) / level_of(1);
    require(gain > 0.5F, "the first arrangement is heard");

    std::atomic<bool> done{false};
    std::atomic<int> refused{0};
    std::thread control([&] {
        for (int generation = 2; generation <= generations; ++generation) {
            // The only owner of this clip's audio is the arrangement slot it
            // is compiled into: letting the slot go frees it.
            auto next = level_song(level_of(generation));
            std::string why;
            if (!engine.recompile(next.song, 0, &why, next.assets)) ++refused;
        }
        done.store(true, std::memory_order_release);
    });

    std::vector<float> left(64), right(64);
    std::size_t blocks = 0;
    float last = 0.0F;
    std::string failure;
    while (!done.load(std::memory_order_acquire) || blocks < 64) {
        engine.process({left, right});
        ++blocks;
        const float level = block_level(left);
        if (failure.empty()) {
            if (level < 0.0F)
                failure = "block " + std::to_string(blocks) + " mixes two arrangements";
            else if (level < last)
                failure = "block " + std::to_string(blocks) + " went back to an older "
                          "arrangement";
            else if (level > level_of(generations) * gain * 1.0001F)
                failure = "block " + std::to_string(blocks) + " played a level no "
                          "arrangement had";
        }
        last = std::max(last, level);
    }
    control.join();
    require(failure.empty(), failure);
    require(refused.load() == 0, "no recompile was refused for want of a free slot: " +
                                     std::to_string(refused.load()));
    // The last arrangement is what plays once the dust settles.
    const float final_level = block_level(render(engine, 64, 64).left);
    require(final_level == level_of(generations) * gain,
            "the last recompile is what plays: heard " + std::to_string(final_level / gain));
    std::cerr << "handoff_stress: " << blocks << " blocks across " << generations
              << " recompiles\n";
}

// --- Takes and the output latency ------------------------------------------

// Scenario: A note struck on an audible beat is written on that beat
void take_on_heard_beat_case() {
    // Track 0 plays a reference note on beat 2 (tick 480) through the CLAP
    // effect's 64 samples of latency, hard left; track 1, hard right, is
    // armed and takes the performance. Everything is heard 64 samples late.
    Pattern reference(1920, 480);
    (void)reference.add(note(480, 240, 60));
    Song song;
    song.patterns = {{"Reference", std::move(reference)}, {"Take", Pattern(1920, 480)}};
    song.tracks = {Track{}, Track{}};
    song.tracks[0].instrument = clap_synth();
    song.tracks[0].inserts = {insert(clap_effect())};
    song.tracks[0].mix.pan = -1.0;
    song.tracks[1].instrument = clap_synth();
    song.tracks[1].mix.pan = 1.0;
    song.tracks[1].input.armed = true;
    song.clips = {{0, 0, 0, 1}, {1, 1, 0, 1}};

    SongEngine engine;
    build(engine, song, 64);
    require(engine.output_latency() == 64, "the CLAP effect's 64 samples are the latency");
    MidiInput keyboard{MidiInput::Mode::deterministic};
    std::string error;
    require(keyboard.open(std::size_t{0}, &error), "the deterministic input opens: " + error);
    keyboard.set_routes(midi_routes(song, 1));
    engine.connect_input(&keyboard.queue());

    // Where the beat is heard: the reference note's onset on the left.
    engine.set_playing(true);
    const auto first_pass = render(engine, 30000, 64);
    const auto heard_beat = onset(first_pass.left);
    const auto beat_sample = static_cast<std::size_t>(engine.published_clock().sample_at(480));
    require(heard_beat == beat_sample + 64,
            "the beat is heard 64 samples after its sample: at " + std::to_string(heard_beat));
    require(heard_beat % 64 == 0, "the heard beat falls on a block boundary");

    // The performer strikes as the beat is heard, on a key and on screen.
    engine.set_playing(false);
    (void)render(engine, 64, 64);
    engine.seek(0);
    engine.set_recording(true);
    engine.set_playing(true);
    (void)render(engine, heard_beat, 64);
    require(keyboard.inject(std::array<std::uint8_t, 3>{0x90, 72, 127}), "a key goes down");
    require(engine.perform(1, {PluginEvent::Type::note_on, 0, 76, 1.0, 0.0}),
            "an on-screen key goes down");
    (void)render(engine, 12000, 64);
    require(keyboard.inject(std::array<std::uint8_t, 3>{0x80, 72, 0}), "the key comes up");
    require(engine.perform(1, {PluginEvent::Type::note_off, 0, 76, 0.0, 0.0}),
            "the on-screen key comes up");
    (void)render(engine, 64, 64);
    engine.set_recording(false);

    TakeRecorder take(1920);
    std::vector<PlayedNote> played;
    CapturedEvent captured;
    int ons = 0;
    while (engine.take_captured(captured)) {
        require(captured.track == 1, "only the armed track captures");
        const auto key = static_cast<std::int16_t>(captured.event.key_or_parameter);
        if (captured.event.type == PluginEvent::Type::note_on) {
            ++ons;
            require(captured.sample == beat_sample && captured.tick == 480,
                    "key " + std::to_string(key) + " is stamped on the beat it was struck to "
                    "(sample " + std::to_string(beat_sample) + ", tick 480), not at sample " +
                    std::to_string(captured.sample) + ", tick " + std::to_string(captured.tick));
            take.note_on(captured.tick, key, static_cast<float>(captured.event.value), 0.0);
        } else if (captured.event.type == PluginEvent::Type::note_off) {
            if (auto finished = take.note_off(captured.tick, key)) played.push_back(*finished);
        }
    }
    require(ons == 2 && played.size() == 2, "both keys were captured whole");

    // Written into the song the way the application writes a take, then
    // played back: the take sounds with the beat, not a latency late.
    for (auto played_note : played) {
        const auto target = take_target(song, 1, played_note.start, 1);
        played_note.start = target.offset;
        (void)write_played(song.patterns[target.pattern].pattern, played_note, 120);
    }
    require(engine.recompile(song, 0, &error), "recompile: " + error);
    engine.set_playing(false);
    (void)render(engine, 64, 64);
    engine.seek(0);
    engine.set_playing(true);
    const auto replay = render(engine, 30000, 64);
    require(onset(replay.left) == heard_beat, "the reference beat is where it was");
    require(onset(replay.right) == heard_beat,
            "the take replays on the beat it was struck to: heard at " +
                std::to_string(onset(replay.right)) + ", the beat at " +
                std::to_string(heard_beat));
    engine.connect_input(nullptr);
}

// --- The main thread's service ----------------------------------------------

template <typename Function>
Function effect_hook(const char* name) {
    void* module = dlopen(BLOKKILY_TEST_CLAP_EFFECT_PATH, RTLD_NOW | RTLD_NOLOAD);
    require(module != nullptr, "the CLAP effect fixture is loaded");
    auto* function = reinterpret_cast<Function>(dlsym(module, name));
    dlclose(module);
    require(function != nullptr, std::string("the fixture exports ") + name);
    return function;
}

// Scenario: Every insert is served on the main thread
void serve_every_processor_case() {
    Song song;
    song.patterns = {{"Served", Pattern(1920, 480)}};
    song.tracks = {Track{}};
    song.tracks[0].instrument = clap_synth();
    song.tracks[0].inserts = {insert(clap_effect())};
    song.returns = {ReturnBus{}};
    song.returns[0].inserts = {insert(builtin("delay")), insert(clap_effect())};
    song.master_inserts = {insert(clap_effect())};
    song.clips = {{0, 0, 0, 1}};
    SongEngine engine;
    build(engine, song);
    const std::vector<ProcessorAddress> wanted{
        track_instrument(0), {BusKind::track, 0, 0}, {BusKind::ret, 0, 0},
        {BusKind::ret, 0, 1}, {BusKind::master, 0, 0}};
    require(engine.processor_addresses() == wanted,
            "the engine names every address that holds a processor, in graph order");

    const auto request = effect_hook<void (*)()>("blokkily_test_effect_request_callback");
    const auto calls = effect_hook<int (*)()>("blokkily_test_effect_main_thread_calls");
    const auto instances = effect_hook<int (*)()>("blokkily_test_effect_instances");
    require(instances() == 3, "a CLAP effect on a track, a return and the master");
    // Each effect asks for the main thread while the song plays; one turn of
    // the service answers every one of them, and only once.
    engine.set_playing(true);
    const int before = calls();
    request();
    (void)render(engine, 1024);
    serve_processors(engine);
    require(calls() - before == 3, "every effect's on_main_thread ran: " +
                                       std::to_string(calls() - before) + " of 3");
    serve_processors(engine);
    require(calls() - before == 3, "a callback runs once per request");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "pass a case name");
        const std::string name = argv[1];
        if (name == "bounce_after_playback") bounce_after_playback_case();
        else if (name == "bounce_resumes_clean") bounce_resumes_clean_case();
        else if (name == "handoff_probe") handoff_probe_case();
        else if (name == "handoff_stress") handoff_stress_case();
        else if (name == "take_on_heard_beat") take_on_heard_beat_case();
        else if (name == "serve_every_processor") serve_every_processor_case();
        else throw std::runtime_error("unknown case " + name);
        std::cout << name << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
