// Executable scenarios for features/sidechain_and_multiout.feature (phase 2,
// wave 5.2): plugin sidechain inputs and multi-output instruments, through the
// real hosts. Each case is its own CTest test.
//
// The processors are the suite-built fixtures, created through the
// production processor factory (populate_graph) and loaded through their
// official entry points: the CLAP effect (input x Gain x (1 - key), 64
// samples late) and the VST3 effect (input x -Gain x (1 - key), 32 samples
// late), each keyed on its own sidechain input; the CLAP and VST3 synths,
// whose aux output carries minus half of their main output. Every claim is
// read off rendered audio: the master bus the production
// SongEngine::process() wrote, or an export read back from disk.

#include "audio/engine/test_access.hpp"
#include "engine_graph.hpp"
#include "support/audio_probe.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/vst3_instance.hpp"
#include "blokkily/project/project.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <numbers>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string text(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 256;
// One tick per sample: 24000 ticks a beat at 120 BPM and 48 kHz.
constexpr Tick ticks_per_beat = 24000;
const float centre = static_cast<float>(std::cos(std::numbers::pi / 4.0));

PluginSlot clap_effect() {
    return {"CLAP", BLOKKILY_TEST_CLAP_EFFECT_PATH, "dev.blokkily.test.effect", {}};
}
PluginSlot vst3_effect() { return {"VST3", BLOKKILY_TEST_VST3_EFFECT_PATH, "", {}}; }
PluginSlot clap_synth() { return {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", {}}; }
PluginSlot vst3_synth() { return {"VST3", BLOKKILY_TEST_VST3_PATH, "", {}}; }

EffectSlot keyed(PluginSlot plugin, std::optional<std::uint32_t> key) {
    EffectSlot slot;
    slot.plugin = std::move(plugin);
    slot.sidechain = key;
    return slot;
}

Song song_of(std::size_t tracks, Tick length = 48000) {
    Song song;
    song.patterns = {{"P", Pattern(length, ticks_per_beat)}};
    song.tracks.assign(tracks, Track{});
    for (std::size_t track = 0; track < tracks; ++track)
        song.tracks[track].name = "T" + std::to_string(track);
    song.clips = {{0, 0, 0, 1}};
    return song;
}

// A steady level added to the track, read from the context each block.
void dc_source(void* context, StereoBlock track, std::uint64_t) noexcept {
    const float level = *static_cast<const float*>(context);
    for (auto& sample : track.left) sample += level;
    for (auto& sample : track.right) sample += level;
}
// Full-scale bursts: on for the first 4800 samples of every 12000, by song
// position.
void burst_source(void*, StereoBlock track, std::uint64_t position) noexcept {
    for (std::size_t frame = 0; frame < track.left.size(); ++frame) {
        const float on = (position + frame) % 12000 < 4800 ? 0.5F : 0.0F;
        track.left[frame] += on;
        track.right[frame] += on;
    }
}

// Every processor the song names, through the production factory; prepared.
void build(SongEngine& engine, const Song& song) {
    const auto graph = populate_graph(engine, song, {}, {});
    require(graph.error.empty(), "every processor must load: " + graph.error);
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "the song must prepare: " + error);
    load_fresh_state(engine, song, graph);
}

struct Stereo {
    std::vector<float> left, right;
};
Stereo render(SongEngine& engine, std::size_t frames) {
    Stereo out{std::vector<float>(frames, 0.0F), std::vector<float>(frames, 0.0F)};
    for (std::size_t start = 0; start < frames; start += block) {
        const auto count = std::min<std::size_t>(block, frames - start);
        engine.process({std::span(out.left).subspan(start, count),
                        std::span(out.right).subspan(start, count)});
    }
    return out;
}

// The steady value of the last `count` samples, required to be steady.
float settled(const std::vector<float>& samples, std::size_t count = 512) {
    const auto tail = std::span<const float>(samples).last(count);
    const auto [low, high] = std::minmax_element(tail.begin(), tail.end());
    require(*high - *low < 1e-5F, "the output settles, but spans " + text(*low) + " .. " +
                                      text(*high));
    return tail.back();
}

bool near(double actual, double expected, double tolerance = 1e-5) {
    return std::abs(actual - expected) <= tolerance;
}

// --- Plugin sidechain inputs -------------------------------------------------

// Both effect fixtures declare a sidechain input; both synths an aux output.
void ports_declared_case() {
    std::string error;
    auto clap = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_EFFECT_PATH,
                                           "dev.blokkily.test.effect", &error);
    require(clap != nullptr, "the CLAP effect loads: " + error);
    require(clap->ports().audio_inputs == 2 && clap->ports().sidechain_inputs == 2 &&
                clap->ports().aux_outputs == 0,
            "the CLAP effect has a stereo main input and a stereo sidechain");
    auto vst3 = Vst3PluginInstance::create(BLOKKILY_TEST_VST3_EFFECT_PATH, 0, &error);
    require(vst3 != nullptr, "the VST3 effect loads: " + error);
    require(vst3->ports().audio_inputs == 2 && vst3->ports().sidechain_inputs == 2 &&
                vst3->ports().aux_outputs == 0,
            "the VST3 effect's aux input bus is enabled as a stereo sidechain, got " +
                text(vst3->ports().sidechain_inputs));
    auto synth = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
    require(synth != nullptr && synth->ports().aux_outputs == 1 &&
                synth->ports().sidechain_inputs == 0 && synth->ports().audio_inputs == 0,
            "the CLAP synth has one aux output and no input");
    auto vst3_synth_instance = Vst3PluginInstance::create(BLOKKILY_TEST_VST3_PATH, 0, &error);
    require(vst3_synth_instance != nullptr && vst3_synth_instance->ports().aux_outputs == 1,
            "the VST3 synth's aux output bus is enabled");
}

// A plugin effect on track 0 keyed from track 1, which is muted, faded to
// -60 dB and has an effect of its own: the key is track 1 after its insert
// and before its fader. `key_effect` is the key track's insert, `level` what
// it does to 0.25 (its gain times its sign), `gain` the keyed effect's own.
void plugin_key(const PluginSlot& keyed_effect, float gain, const PluginSlot& key_effect,
                float key_gain, const char* name) {
    auto song = song_of(2);
    song.tracks[0].inserts = {keyed(keyed_effect, 1)};
    song.tracks[1].inserts = {keyed(key_effect, std::nullopt)};
    song.tracks[1].mix.mute = true;
    song.tracks[1].mix.gain_db = -60.0;
    require(song.consistent(), "the song is valid");
    SongEngine engine;
    build(engine, song);
    float source = 1.0F;
    float key = 0.0F;
    engine::TestAccess::set_test_source(engine, 0, dc_source, &source);
    engine::TestAccess::set_test_source(engine, 1, dc_source, &key);
    engine.set_playing(true);

    const float open = settled(render(engine, 4096).left);
    require(near(open, gain * centre),
            std::string(name) + ": a silent key leaves the track at its gain, got " + text(open));
    key = 0.25F;
    const float ducked = settled(render(engine, 4096).left);
    // The key after the key track's insert: 0.25 x |key_gain|.
    const double key_level = 0.25 * std::abs(key_gain);
    const double expected = gain * (1.0 - key_level) * centre;
    require(near(ducked, expected, 1e-4),
            std::string(name) + ": the muted, faded key ducks through the plugin's sidechain " +
                "input by its post-insert level: got " + text(ducked) + ", expected " +
                text(expected) + " (a pre-insert key would give " +
                text(gain * 0.75 * centre) + ")");
    key = 0.0F;
    const float released = settled(render(engine, 4096).left);
    require(near(released, open), std::string(name) + ": the track opens when the key stops");
    // No key named: the plugin's sidechain input hears silence.
    song.tracks[0].inserts[0].sidechain.reset();
    std::string error;
    require(engine.recompile(song, 0, &error), "unkeying recompiles: " + error);
    key = 0.25F;
    const float unkeyed = settled(render(engine, 4096).left);
    require(near(unkeyed, open), std::string(name) + ": an insert with no key is not ducked");
    std::cerr << name << ": open " << open << " ducked " << ducked << " expected " << expected
              << '\n';
}

void clap_key_case() { plugin_key(clap_effect(), 0.25F, vst3_effect(), -0.25F, "CLAP"); }
void vst3_key_case() { plugin_key(vst3_effect(), -0.25F, clap_effect(), 0.25F, "VST3"); }

// An export of a song whose CLAP and VST3 inserts are keyed from a muted
// track of bursts equals the live render and pumps with the key.
void plugin_key_bounce_case() {
    auto song = song_of(3);
    song.tracks[0].inserts = {keyed(clap_effect(), 2)};
    song.tracks[1].inserts = {keyed(vst3_effect(), 2)};
    song.tracks[0].mix.pan = 1.0;  // the CLAP-keyed track hard right
    song.tracks[1].mix.pan = -1.0; // the VST3-keyed track hard left
    song.tracks[2].mix.mute = true;
    SongEngine engine;
    build(engine, song);
    float source = 1.0F;
    engine::TestAccess::set_test_source(engine, 0, dc_source, &source);
    engine::TestAccess::set_test_source(engine, 1, dc_source, &source);
    engine::TestAccess::set_test_source(engine, 2, burst_source, nullptr);
    std::filesystem::create_directories(BLOKKILY_TEST_ARTIFACTS);
    const auto file = std::filesystem::path(BLOKKILY_TEST_ARTIFACTS) / "plugin-sidechain.wav";
    std::string error;
    const auto report = bounce_song(engine, file, WaveFormat::float32, 0, &error);
    require(report.has_value(), "the bounce is written: " + error);
    const auto wave = read_wave(file, &error);
    const auto song_frames = static_cast<std::size_t>(engine.song_samples());
    require(wave.has_value() && wave->channels == 2 && wave->frames >= song_frames,
            "the bounce reads back: " + error);
    // The export starts where the song does; playback reaches the speakers
    // the engine's latency later.
    const auto latency = static_cast<std::size_t>(engine.output_latency());
    engine.seek(0);
    engine.set_playing(true);
    const auto live = render(engine, song_frames + latency);
    double worst = 0.0;
    for (std::size_t frame = 0; frame < song_frames; ++frame) {
        worst = std::max(worst, std::abs(static_cast<double>(wave->interleaved[2 * frame] -
                                                             live.left[frame + latency])));
        worst = std::max(worst, std::abs(static_cast<double>(wave->interleaved[2 * frame + 1] -
                                                             live.right[frame + latency])));
    }
    require(worst < 1e-6, "the export equals the live render, worst " + text(worst));
    // Hard right is the CLAP track, hard left the VST3 track. A burst of 0.5
    // halves each; between bursts each plays its gain. The latencies (64 and
    // 32, compensated to 64 on every track) move the bursts, so the middle of
    // a burst and of a gap are read.
    const auto sample = [&](std::size_t frame, int channel) {
        return wave->interleaved[2 * frame + static_cast<std::size_t>(channel)];
    };
    for (const std::size_t cycle : {std::size_t{1}, std::size_t{2}}) {
        const std::size_t during = cycle * 12000 + 2400;
        const std::size_t between = cycle * 12000 + 8400;
        require(near(sample(during, 1), 0.125, 1e-4) && near(sample(between, 1), 0.25, 1e-4),
                "the CLAP insert pumps with the key in the export: " + text(sample(during, 1)) +
                    " / " + text(sample(between, 1)));
        require(near(sample(during, 0), -0.125, 1e-4) && near(sample(between, 0), -0.25, 1e-4),
                "the VST3 insert pumps with the key in the export: " + text(sample(during, 0)) +
                    " / " + text(sample(between, 0)));
    }
}


// --- Multi-output instruments -------------------------------------------------

// A synth on track 0, hard left, playing one note from 0 to `note_end`, and
// its aux output broken out to track 1, hard right: the master's left side is
// the main output alone and its right side the aux channel alone.
Song multi_out_song(const PluginSlot& synth, Tick note_end = 47000) {
    auto song = song_of(2);
    song.tracks[0].name = "SYNTH";
    song.tracks[0].instrument = synth;
    song.tracks[0].mix.pan = -1.0;
    song.tracks[1].name = "SYNTH AUX 1";
    song.tracks[1].source = InstrumentOutput{0, 1};
    song.tracks[1].mix.pan = 1.0;
    Trigger note;
    note.start = 0;
    note.duration = note_end;
    note.musical_data = Note{60, 1.0F, 0.0F};
    (void)song.patterns[0].pattern.add(note);
    return song;
}

// The synths sound Level (0.25) while a key is down; their aux output carries
// minus half of it.
void multi_out(const PluginSlot& synth, const char* name) {
    auto song = multi_out_song(synth);
    std::string why;
    require(song.consistent(&why), "the song is valid: " + why);
    SongEngine engine;
    build(engine, song);
    engine.set_playing(true);
    auto out = render(engine, 8192);
    const float main = settled(out.left, 1024);
    const float aux = settled(out.right, 1024);
    require(near(main, 0.25) && near(aux, -0.125),
            std::string(name) + ": the main output reaches only the source track (0.25, not " +
                "0.125 with the aux mixed in) and the aux output only its channel (-0.125): got " +
                text(main) + " / " + text(aux));

    // Live moves on the aux channel reach the engine without a rebuild and
    // touch the aux signal alone.
    auto* instrument = engine.processor(track_instrument(0));
    song.tracks[1].mix.gain_db = -6.0;
    engine.apply_mix(song);
    out = render(engine, 4096);
    const double faded = -0.125 * db_to_linear(-6.0);
    require(near(settled(out.right), faded, 1e-4) && near(settled(out.left), 0.25),
            std::string(name) + ": the aux channel's fader moves only the aux signal");
    song.tracks[1].mix.mute = true;
    engine.apply_mix(song);
    out = render(engine, 4096);
    require(near(settled(out.right), 0.0) && near(settled(out.left), 0.25),
            std::string(name) + ": muting the aux channel removes the aux signal and only it: " +
                text(settled(out.right)) + " / " + text(settled(out.left)));
    song.tracks[1].mix.mute = false;
    song.tracks[1].mix.gain_db = 0.0;
    song.tracks[0].mix.mute = true;
    engine.apply_mix(song);
    out = render(engine, 4096);
    require(near(settled(out.right), -0.125) && near(settled(out.left), 0.0),
            std::string(name) + ": muting the source track leaves its aux channel sounding");
    song.tracks[0].mix.mute = false;
    engine.apply_mix(song);
    require(engine.processor(track_instrument(0)) == instrument,
            std::string(name) + ": no move replaced the instrument");

    // Unrouted, the aux output is rendered and dropped: never doubled into
    // the main output.
    song.tracks[1].source.reset();
    require(engine.recompile(song, 0, &why), "unrouting recompiles: " + why);
    // A recompile lets go of the sounding note; the song is played again
    // from its start, where the note is struck.
    engine.seek(0);
    out = render(engine, 4096);
    require(near(settled(out.left), 0.25) && near(settled(out.right), 0.0),
            std::string(name) + ": an aux output with no channel is not heard at all");
    // Routed again by a recompile: the running engine, the same instrument.
    song.tracks[1].source = InstrumentOutput{0, 1};
    require(engine.recompile(song, 0, &why), "routing recompiles: " + why);
    engine.seek(0);
    out = render(engine, 4096);
    require(near(settled(out.right), -0.125) && engine.processor(track_instrument(0)) == instrument,
            std::string(name) + ": routing the output again brings it back");
    std::cerr << name << ": main " << main << ", aux " << aux << '\n';
}

void clap_multi_out_case() { multi_out(clap_synth(), "CLAP"); }
void vst3_multi_out_case() { multi_out(vst3_synth(), "VST3"); }

// The aux channel comes before its source in the song and the render order
// still puts the source first: the channel hears this chunk's output.
void multi_out_order_case() {
    auto song = multi_out_song(clap_synth());
    std::swap(song.tracks[0], song.tracks[1]);
    song.tracks[0].source = InstrumentOutput{1, 1};
    song.clips = {{1, 0, 0, 1}};
    std::string why;
    require(song.consistent(&why), "the song is valid: " + why);
    SongEngine engine;
    build(engine, song);
    engine.set_playing(true);
    // The note starts at sample 0: a channel rendered before its source
    // would hear nothing in the first chunk.
    const auto out = render(engine, block);
    require(near(out.left[0], 0.25) && near(out.right[0], -0.125) &&
                near(out.right[block - 1], -0.125),
            "the channel before its source in the song hears the same chunk: " +
                text(out.right[0]));
}

// An export of a multi-output song with an effect and a send on the aux
// channel equals the live render.
void multi_out_bounce(const PluginSlot& synth, const char* name, const char* file_name) {
    // The note sounds for the first half of the song only.
    auto song = multi_out_song(synth, 24000);
    song.tracks[1].inserts = {keyed(clap_effect(), std::nullopt)};
    song.returns = {ReturnBus{}};
    song.returns[0].mix.pan = 1.0;
    song.tracks[1].sends = {Send{0, -6.0, false}};
    SongEngine engine;
    build(engine, song);
    std::filesystem::create_directories(BLOKKILY_TEST_ARTIFACTS);
    const auto file = std::filesystem::path(BLOKKILY_TEST_ARTIFACTS) / file_name;
    std::string error;
    const auto report = bounce_song(engine, file, WaveFormat::float32, 0, &error);
    require(report.has_value(), "the bounce is written: " + error);
    const auto wave = read_wave(file, &error);
    const auto song_frames = static_cast<std::size_t>(engine.song_samples());
    require(wave.has_value() && wave->channels == 2 && wave->frames >= song_frames,
            "the bounce reads back: " + error);
    const auto latency = static_cast<std::size_t>(engine.output_latency());
    engine.seek(0);
    engine.set_playing(true);
    const auto live = render(engine, song_frames + latency);
    double worst = 0.0;
    for (std::size_t frame = 0; frame < song_frames; ++frame) {
        worst = std::max(worst, std::abs(static_cast<double>(wave->interleaved[2 * frame] -
                                                             live.left[frame + latency])));
        worst = std::max(worst, std::abs(static_cast<double>(wave->interleaved[2 * frame + 1] -
                                                             live.right[frame + latency])));
    }
    require(worst < 1e-6, std::string(name) + ": the export equals the live render, worst " +
                              text(worst));
    // While the note sounds: the main output on the left; on the right the
    // aux through the CLAP effect (x 0.25), plus that at -6 dB through the
    // send (post-fader, pre-pan) into a hard-right return (whose pan passes
    // all of it to the right).
    const auto at = [&](std::size_t frame, int channel) {
        return static_cast<double>(wave->interleaved[2 * frame + static_cast<std::size_t>(channel)]);
    };
    const double aux = -0.125 * 0.25;
    const double expected_right = aux + aux * db_to_linear(-6.0);
    require(near(at(12000, 0), 0.25, 1e-5) && near(at(12000, 1), expected_right, 1e-5),
            std::string(name) + ": the export has the main output left and the processed aux " +
                "channel right: " + text(at(12000, 0)) + " / " + text(at(12000, 1)) +
                ", expected " + text(expected_right));
    require(near(at(36000, 0), 0.0) && near(at(36000, 1), 0.0),
            std::string(name) + ": both are silent once the note ends");
}

void multi_out_bounce_case() {
    multi_out_bounce(clap_synth(), "CLAP", "multi-out-clap.wav");
    multi_out_bounce(vst3_synth(), "VST3", "multi-out-vst3.wav");
}

// The song refuses an output it could not play, keeps outputs pointing at
// the right tracks when one is removed, and saves them.
void multi_out_song_model_case() {
    const auto base = multi_out_song(clap_synth());
    std::string why;
    require(base.consistent(&why), "the multi-output song is valid: " + why);
    const auto refused = [&](const std::function<void(Song&)>& edit, const std::string& rule) {
        auto song = base;
        edit(song);
        std::string reason;
        require(!song.consistent(&reason), "refused: " + rule);
        std::cerr << "refused (" << rule << "): " << reason << '\n';
    };
    refused([](Song& song) { song.tracks[1].source->track = 7; }, "a missing source track");
    refused([](Song& song) { song.tracks[1].source->track = 1; }, "its own instrument");
    refused([](Song& song) { song.tracks[1].source->output = 0; }, "output 0 is the main one");
    refused([](Song& song) { song.tracks[1].instrument = clap_synth(); },
            "a fed track with an instrument of its own");
    refused([](Song& song) {
        song.tracks.push_back(Track{});
        song.tracks[2].source = InstrumentOutput{0, 1};
    }, "one output feeding two tracks");
    refused([](Song& song) {
        song.tracks[0].inserts = {keyed(clap_effect(), 1)};
    }, "a key from the channel of the keyed track's own instrument (a loop)");
    {
        // A second output of the same instrument on another track is fine.
        auto song = base;
        song.tracks.push_back(Track{});
        song.tracks[2].source = InstrumentOutput{0, 2};
        require(song.consistent(&why), "two outputs, two channels: " + why);
    }
    {
        // A track removed before the source: the channel follows its source.
        auto song = base;
        song.tracks.insert(song.tracks.begin(), Track{});
        song.tracks[2].source = InstrumentOutput{1, 1};
        for (auto& clip : song.clips) ++clip.track;
        require(song.consistent(&why), "shifted: " + why);
        require(!song.remove_track(0).empty(), "the first track goes");
        require(song.tracks[1].source == InstrumentOutput{0, 1} && song.consistent(&why),
                "the channel's source follows its track: " + why);
        // The source removed: the channel keeps its strip and loses its source.
        song.tracks[1].mix.gain_db = -3.0;
        require(!song.remove_track(0).empty(), "the source goes");
        require(song.tracks.size() == 1 && !song.tracks[0].source &&
                    song.tracks[0].mix.gain_db == -3.0 && song.consistent(&why),
                "the channel of a removed instrument becomes a plain track");
    }
    {
        // Saved and read back exactly; a file without the record (every
        // file saved before it) loads with no channel; a dangling one is
        // refused.
        Project project;
        project.song = base;
        const auto text_saved = ProjectFile::serialize(project);
        require(text_saved.find("auxsource 1 0 1\n") != std::string::npos,
                "the channel is saved as an auxsource record");
        std::string error;
        const auto loaded = ProjectFile::parse(text_saved, &error);
        require(loaded.has_value() && loaded->song.tracks.size() == 2 &&
                    loaded->song.tracks[1].source == InstrumentOutput{0, 1} &&
                    !loaded->song.tracks[0].source,
                "the channel reads back: " + error);
        auto old = text_saved;
        old.erase(old.find("auxsource 1 0 1\n"), std::string("auxsource 1 0 1\n").size());
        const auto older = ProjectFile::parse(old, &error);
        require(older.has_value() && !older->song.tracks[1].source,
                "a file saved before instrument outputs loads: " + error);
        auto dangling = text_saved;
        dangling.replace(dangling.find("auxsource 1 0 1"), 15, "auxsource 1 5 1");
        require(!ProjectFile::parse(dangling, &error).has_value(),
                "a file whose channel names a missing track is refused");
        std::cerr << "dangling file: " << error << '\n';
    }
}

// Soloing an instrument track keeps its aux output channels audible: they
// are that instrument's sound, broken out. A third track, soloed out, is the
// control that proves the solo is on. Soloing only the aux channel plays the
// channel alone, not its source's main output. Read off the master bus; the
// same through an export, which renders through this engine.
void multi_out_solo_case() {
    auto song = multi_out_song(clap_synth());
    song.tracks.push_back(Track{});
    song.tracks[2].name = "OTHER";
    song.tracks[2].instrument = clap_synth();
    song.tracks[2].mix.pan = 0.0;
    song.clips.push_back({2, 0, 0, 1});
    std::string why;
    require(song.consistent(&why), "the song is valid: " + why);
    SongEngine engine;
    build(engine, song);
    engine.set_playing(true);
    auto out = render(engine, 8192);
    const float other = 0.25F * centre;
    require(near(settled(out.left, 1024), 0.25 + other, 1e-4) &&
                near(settled(out.right, 1024), -0.125 + other, 1e-4),
            "unsoloed, all three are heard: " + text(settled(out.left)) + " / " +
                text(settled(out.right)));

    song.tracks[0].mix.solo = true;
    require(song.soloed(0) && song.soloed(1) && !song.soloed(2),
            "soloing the instrument solos its aux channel with it");
    engine.apply_mix(song);
    out = render(engine, 4096);
    require(near(settled(out.left), 0.25, 1e-4) && near(settled(out.right), -0.125, 1e-4),
            "soloing the source keeps its aux channel sounding and silences the other track: " +
                text(settled(out.left)) + " / " + text(settled(out.right)));

    song.tracks[0].mix.solo = false;
    song.tracks[1].mix.solo = true;
    engine.apply_mix(song);
    out = render(engine, 4096);
    require(near(settled(out.left), 0.0, 1e-4) && near(settled(out.right), -0.125, 1e-4),
            "soloing the aux channel alone plays the channel, not its source's main output: " +
                text(settled(out.left)) + " / " + text(settled(out.right)));

    // Solo on the source with the aux channel muted: mute still wins.
    song.tracks[1].mix.solo = false;
    song.tracks[0].mix.solo = true;
    song.tracks[1].mix.mute = true;
    engine.apply_mix(song);
    out = render(engine, 4096);
    require(near(settled(out.left), 0.25, 1e-4) && near(settled(out.right), 0.0, 1e-4),
            "a muted aux channel stays muted under its source's solo");

    // An export of the soloed song hears what the engine plays.
    song.tracks[1].mix.mute = false;
    engine.apply_mix(song);
    const auto file = std::filesystem::path(BLOKKILY_TEST_ARTIFACTS) / "multi-out-solo.wav";
    std::filesystem::create_directories(file.parent_path());
    std::string error;
    const auto report = bounce_song(engine, file, WaveFormat::float32, 0, &error);
    require(report.has_value(), "the soloed song bounces: " + error);
    const auto read = read_wave(file, &error);
    require(read.has_value() && read->channels == 2 && read->frames > 12000,
            "the bounce reads back: " + error);
    const double left = read->interleaved[2 * 12000];
    const double right = read->interleaved[2 * 12000 + 1];
    require(near(left, 0.25, 1e-4) && near(right, -0.125, 1e-4),
            "the export keeps the aux channel under its source's solo and drops the other: " +
                text(left) + " / " + text(right));
}

using Case = std::function<void()>;
const std::map<std::string, Case>& cases() {
    static const std::map<std::string, Case> all{
        {"ports_declared", ports_declared_case},
        {"clap_key", clap_key_case},
        {"vst3_key", vst3_key_case},
        {"plugin_key_bounce", plugin_key_bounce_case},
        {"clap_multi_out", clap_multi_out_case},
        {"vst3_multi_out", vst3_multi_out_case},
        {"multi_out_order", multi_out_order_case},
        {"multi_out_bounce", multi_out_bounce_case},
        {"multi_out_song_model", multi_out_song_model_case},
        {"multi_out_solo", multi_out_solo_case},
    };
    return all;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: blokkily_plugin_routing_tests <case>\n";
        return 2;
    }
    const std::string name = argv[1];
    const auto found = cases().find(name);
    if (found == cases().end()) {
        std::cerr << "Unknown case: " << name << "\n";
        return 2;
    }
    try {
        found->second();
    } catch (const std::exception& ex) {
        std::cerr << "FAILED: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "PASS " << name << '\n';
    return 0;
}
