// Audio input into audio clips (plan item 3.2, feature 4c), proved from what
// the production render callback rendered through the deterministic pump -
// with input injected into the device, or carried back from its outputs by a
// modelled loopback cable with a known round trip - from the production
// bounce read back from its file, and from the take files read back.
//
// Run with a case name; each case is its own CTest test (audio_input_<case>).
// features/audio_input.feature maps its scenarios to these cases.

#include "engine_graph.hpp"
#include "processor_factory.hpp"
#include "support/audio_probe.hpp"

#include "blokkily/audio/audio_asset.hpp"
#include "blokkily/audio/audio_clips.hpp"
#include "blokkily/audio/audio_input.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/take_writer.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;
namespace fs = std::filesystem;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 512;
// The modelled device's round trip, and the loopback cable's delay.
constexpr std::uint32_t round_trip = 256;
// Where the click is: its clip starts on tick 1000 (sample 50000 at 120 BPM,
// 480 ticks a beat, 48 kHz) and the click is 37 frames into the file, off
// every tick.
constexpr Tick click_tick = 1000;
constexpr std::uint64_t click_in_file = 37;
constexpr std::uint64_t click_sample = 50000 + click_in_file;
constexpr float click_level = 0.8F;
constexpr std::uint64_t click_frames = 4800;

fs::path work_dir(const std::string& name) {
    const fs::path dir = fs::path(BLOKKILY_AUDIO_INPUT_WORK) / name;
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir);
    return dir;
}

// Instrument-less tracks and one empty pattern: the clips decide the length.
Song input_song(std::size_t tracks) {
    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks.assign(tracks, Track{});
    for (std::size_t track = 0; track < tracks; ++track)
        song.tracks[track].name = "T" + std::to_string(track);
    song.clips = {};
    return song;
}

// A file holding one click (and a quieter one after it, so a take is known to
// be the whole click track and not one sample that happens to line up).
AudioFileRef write_click(const fs::path& file) {
    std::vector<float> samples(click_frames, 0.0F);
    samples[click_in_file] = click_level;
    samples[click_in_file + 2000] = -0.5F;
    WaveWriter writer;
    std::string error;
    require(writer.open(file, static_cast<std::uint32_t>(rate), WaveFormat::float32, &error) &&
                writer.write(samples, samples, &error) && writer.close(&error),
            "write the click: " + error);
    return {file, click_frames, static_cast<std::uint32_t>(rate), 2};
}

void add_clip(Song& song, std::size_t file, std::size_t track, Tick start,
              std::uint64_t length) {
    AudioClip clip;
    clip.id = song.next_audio_clip_id();
    clip.track = track;
    clip.file = file;
    clip.start = start;
    clip.length_frames = length;
    song.audio_clips.push_back(clip);
}

struct Stereo {
    std::vector<float> left, right;
};

// The production callback, `frames` long, in blocks of `size`, with `inject`
// (frame -> value per input channel) played into the device's inputs.
Stereo pump(RtAudioOutput& output, std::size_t frames, std::size_t size,
            const std::function<float(std::uint32_t, std::uint64_t)>& inject = {},
            std::uint64_t first_frame = 0) {
    Stereo out{std::vector<float>(frames), std::vector<float>(frames)};
    const auto channels = output.input_channels();
    for (std::size_t done = 0; done < frames;) {
        const auto count = std::min(size, frames - done);
        std::vector<float> stereo(count * 2, -9.0F);
        std::vector<float> injected;
        if (inject && channels > 0) {
            injected.resize(channels * count);
            for (std::uint32_t channel = 0; channel < channels; ++channel)
                for (std::size_t frame = 0; frame < count; ++frame)
                    injected[channel * count + frame] = inject(channel, first_frame + done + frame);
        }
        require(output.pump(stereo, injected), "the production callback must render");
        std::copy_n(stereo.begin(), count, out.left.begin() + static_cast<std::ptrdiff_t>(done));
        std::copy_n(stereo.begin() + static_cast<std::ptrdiff_t>(count), count,
                    out.right.begin() + static_cast<std::ptrdiff_t>(done));
        done += count;
    }
    return out;
}

// Reads a bounce or a take back from disk, one vector per channel.
std::vector<std::vector<float>> read_back(const fs::path& file) {
    std::string error;
    const auto wave = read_wave(file, &error);
    require(wave.has_value(), "read back " + file.string() + ": " + error);
    std::vector<std::vector<float>> channels(wave->channels,
                                             std::vector<float>(wave->frames, 0.0F));
    for (std::uint64_t frame = 0; frame < wave->frames; ++frame)
        for (std::size_t channel = 0; channel < wave->channels; ++channel)
            channels[channel][frame] = wave->interleaved[frame * wave->channels + channel];
    return channels;
}

// Builds every processor the song names through the production factory and
// prepares the engine with the song's clips decoded from disk.
AudioAssets build(SongEngine& engine, const Song& song, AudioAssetCache& cache) {
    const auto graph = populate_graph(engine, song, {}, {});
    require(graph.error.empty(), "every processor must load: " + graph.error);
    ClipAssetReport report;
    auto assets = load_clip_assets(song, cache, rate, &report);
    require(report.missing_count() == 0, "every clip's file must load");
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error, assets), "prepare: " + error);
    load_fresh_state(engine, song, graph);
    return assets;
}

EffectSlot clap_effect() {
    return {{"CLAP", BLOKKILY_TEST_CLAP_EFFECT_PATH, "dev.blokkily.test.effect", {}}, false, {}};
}

// --- cases -------------------------------------------------------------------

// Scenario: Which tracks hear and record the inputs
void routes() {
    auto song = input_song(4);
    song.tracks[0].input.source = TrackInput::Source::midi;
    song.tracks[1].input = {false, TrackInput::Source::audio, -1, 0, 2,
                            TrackInput::Monitor::automatic};
    song.tracks[2].input = {false, TrackInput::Source::midi_and_audio, -1, 3, 1,
                            TrackInput::Monitor::on};
    song.tracks[3].input = {false, TrackInput::Source::audio, -1, 1, 1,
                            TrackInput::Monitor::off};
    // Nothing armed: the selected audio track records (decision 4); nothing
    // is monitored automatically, only a monitor that is on.
    auto routed = audio_input_routes(song, 1);
    require(routed[0] == AudioInputRoute{}, "a MIDI track takes no audio");
    require(routed[1] == AudioInputRoute{0, 2, false, true},
            "the selected audio track records inputs 1-2, unmonitored while unarmed");
    require(routed[2] == AudioInputRoute{3, 1, true, false},
            "a track whose monitor is on hears input 4 without recording it");
    require(routed[3] == AudioInputRoute{1, 1, false, false}, "an unselected track records nothing");
    // A MIDI track selected with nothing armed records no audio.
    routed = audio_input_routes(song, 0);
    require(!routed[1].capture && !routed[2].capture, "only the selected track may record");
    // Armed: the armed tracks record, automatic monitoring follows the arm.
    song.tracks[3].input.armed = true;
    song.tracks[1].input.armed = true;
    routed = audio_input_routes(song, 2);
    require(routed[1] == AudioInputRoute{0, 2, true, true}, "armed, auto-monitored, recording");
    require(routed[3] == AudioInputRoute{1, 1, false, true}, "armed with the monitor off");
    require(!routed[2].capture && routed[2].monitor,
            "with tracks armed, the selected unarmed track does not record");
}

// Scenarios: A take is placed where it was heard (loopback) / through a latent
// master insert.
void loopback(bool latent_master) {
    const auto dir = work_dir(latent_master ? "latent_master_insert" : "loopback_click");
    auto song = input_song(2);
    song.audio_files = {write_click(dir / "click.wav")};
    add_clip(song, 0, 0, click_tick, click_frames);
    song.tracks[0].mix.pan = -1.0;   // the click, alone on the left
    song.tracks[1].mix.pan = 1.0;    // what is recorded, alone on the right
    song.tracks[1].input = {true, TrackInput::Source::audio, -1, 0, 1, TrackInput::Monitor::off};
    if (latent_master) song.master_inserts = {clap_effect()};
    // What the effect does to what passes it: 64 samples late, at 0.25.
    const std::uint64_t pdc = latent_master ? 64 : 0;
    const float through = latent_master ? 0.25F : 1.0F;

    // The pass: the click plays out of the device and back into input 1
    // through the cable, while track 1 records it.
    SongEngine engine;
    AudioAssetCache cache;
    (void)build(engine, song, cache);
    require(engine.output_latency() == pdc,
            "the engine's output latency is " + std::to_string(engine.output_latency()));
    RtAudioOutput output{RtAudioOutput::Mode::deterministic};
    output.model_input(2, round_trip, true);
    require(output.open(engine) && output.start(), "the production output must start");
    require(output.input_channels() == 2 && output.round_trip_latency() == round_trip,
            "the modelled device has two inputs and a 256-frame round trip");
    TakeWriter writer;
    writer.begin(dir / "takes", static_cast<std::uint32_t>(rate));
    engine.connect_capture(&writer.ring());
    engine.set_audio_inputs(audio_input_routes(song, 0));
    engine.set_recording(true);
    engine.set_playing(true);
    const auto song_samples = engine.song_samples();
    // Blocks of an awkward size, so no boundary lines up with anything.
    const auto played = pump(output, song_samples, 700);
    engine.set_playing(false);
    engine.set_recording(false);
    auto takes = writer.finish();
    engine.connect_capture(nullptr);
    require(writer.dropped_frames() == 0, "nothing was dropped");
    require(probe::first_nonzero(played.left, 0.01F) == click_sample + pdc,
            "the click left the device at " + std::to_string(click_sample + pdc));
    require(takes.size() == 1, "one take, not " + std::to_string(takes.size()));
    const auto& take = takes.front();
    require(take.track == 1 && take.channels == 1 && take.first_sample == 0 &&
                take.frames == song_samples && take.error.empty(),
            "the take is track 1's mono input from the first sample to the end of the song");

    // The file on disk is exactly what the input received: the click, a
    // round trip after it left the device.
    const auto file = read_back(take.file);
    require(file.size() == 1 && file[0].size() == song_samples, "a mono file of the whole pass");
    const auto arrived = click_sample + pdc + round_trip;
    require(probe::first_nonzero(file[0], 0.01F) == arrived &&
                std::abs(file[0][arrived] - click_level * through) < 1e-6F,
            "the take holds the click where the input received it, at " +
                std::to_string(arrived));

    // Placed a round trip plus the output latency earlier (plan C21).
    const auto compensation =
        take_compensation(output.round_trip_latency(), engine.output_latency(),
                          song.record_offset_samples);
    require(compensation == round_trip + pdc, "compensation is the round trip plus the PDC");
    const auto id = commit_take(song, take, engine.published_clock(), compensation);
    require(id.has_value() && song.audio_clips.size() == 2 && song.audio_files.size() == 2,
            "the take became a clip playing a new file");
    const auto& clip = song.audio_clips.back();
    require(clip.track == 1 && clip.file == 1, "the clip is on the recording track");
    const TickClock clock(song.tempo, song.ticks_per_beat(), rate);
    // Captured from sample 0, its first `compensation` frames were heard
    // before the song began: they are skipped, not moved.
    require(clip.start == 0 && clip.offset_frames == compensation &&
                clip.length_frames == take.frames - compensation,
            "the clip starts at 0 with the frames heard before the song skipped");
    // The song's own correction moves it further: 10 samples earlier.
    {
        auto corrected = song;
        corrected.record_offset_samples = 10;
        const auto moved = place_take(
            clock, 5000, 1000,
            take_compensation(round_trip, pdc, corrected.record_offset_samples));
        require(moved && sample_for_tick(clock, static_cast<double>(moved->start)) -
                                 moved->offset_frames ==
                             5000 - round_trip - pdc - 10,
                "record_offset_samples moves a take by exactly that many samples");
    }

    // Bounced, read back: the recorded click is on the right exactly where
    // the original is on the left.
    SongEngine bouncer;
    AudioAssetCache fresh;
    (void)build(bouncer, song, fresh);
    std::string error;
    const auto bounce = dir / "bounce.wav";
    require(bounce_song(bouncer, bounce, WaveFormat::float32, 0, &error).has_value(),
            "bounce: " + error);
    const auto mix = read_back(bounce);
    require(mix.size() == 2, "a stereo bounce");
    const auto original = probe::first_nonzero(mix[0], 0.01F);
    const auto recorded = probe::first_nonzero(mix[1], 0.01F);
    require(original && recorded, "both clicks are in the bounce");
    const auto apart = static_cast<long long>(*recorded) - static_cast<long long>(*original);
    std::cout << "original click at " << *original << ", recorded click at " << *recorded
              << " (" << apart << " apart)\n";
    require(*original == click_sample, "the original click is on its own sample");
    require(std::llabs(apart) <= 1, "the recorded click is within one sample of the original, "
                                    "not " + std::to_string(apart));
    require(std::abs(mix[1][*recorded] - click_level * through * through) < 1e-5F,
            "the recorded click has the level it was recorded at");
    // The second, quieter click lines up too: the whole take is placed, not
    // one sample of it.
    const auto second = click_sample + 2000;
    require(std::abs(mix[0][second] + 0.5F * through) < 1e-5F &&
                std::abs(mix[1][second] + 0.5F * through * through) < 1e-5F,
            "the second click is on the same sample on both sides");
}

void loopback_click() { loopback(false); }
void latent_master_insert() { loopback(true); }

// Scenario: A ring the writer cannot keep up with drops, never stalls
void overflow() {
    const auto dir = work_dir("overflow");
    auto song = input_song(1);
    song.clips = {{0, 0, 0, 1}};   // one bar: 96000 samples
    song.tracks[0].input = {true, TrackInput::Source::audio, -1, 0, 1, TrackInput::Monitor::on};
    SongEngine engine;
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    RtAudioOutput output{RtAudioOutput::Mode::deterministic};
    output.model_input(1);
    require(output.open(engine) && output.start(), "the production output must start");
    // Room for 4096 mono frames, and no writer thread emptying it.
    TakeWriter writer(4096);
    writer.begin(dir, static_cast<std::uint32_t>(rate), false);
    engine.connect_capture(&writer.ring());
    engine.set_audio_inputs(audio_input_routes(song, 0));
    engine.set_recording(true);
    engine.set_playing(true);
    // Every input frame names itself, so a frame heard or written says which
    // one it was.
    const auto named = [](std::uint32_t, std::uint64_t frame) {
        return static_cast<float>(frame + 1) / 1.0e6F;
    };
    const float centre = strip_gain(MixerStrip{}, false).left;
    // Ten blocks with nobody draining: the ring fills after four.
    const auto first = pump(output, 10240, 1024, named);
    require(writer.dropped_frames() == 10240 - 4096,
            "6144 frames were dropped and counted, not " + std::to_string(writer.dropped_frames()));
    // The callback kept going: the last frame was still monitored, on time.
    require(std::abs(first.left.back() - named(0, 10239) * centre) < 1e-7F,
            "the callback kept rendering and monitoring while the ring was full");
    // The writer catches up; four more blocks fit.
    writer.drain();
    (void)pump(output, 4096, 1024, named, 10240);
    const auto takes = writer.finish();
    engine.connect_capture(nullptr);
    require(takes.size() == 2, "the gap split the recording into two takes, not " +
                                   std::to_string(takes.size()));
    require(takes[0].first_sample == 0 && takes[0].frames == 4096 &&
                takes[1].first_sample == 10240 && takes[1].frames == 4096,
            "each take starts at the song sample its first frame was captured at");
    for (const auto& take : takes) {
        const auto file = read_back(take.file);
        require(file.size() == 1 && file[0].size() == take.frames, "a mono file per take");
        for (std::size_t frame = 0; frame < file[0].size(); ++frame)
            require(file[0][frame] == named(0, take.first_sample + frame),
                    "frame " + std::to_string(frame) + " of the take after sample " +
                        std::to_string(take.first_sample) + " is the frame played there");
    }
    // Placed, the second take starts where it was captured, not where the
    // first one ended.
    const TickClock clock(song.tempo, song.ticks_per_beat(), rate);
    const auto placed = place_take(clock, takes[1].first_sample, takes[1].frames, 0);
    require(placed && sample_for_tick(clock, static_cast<double>(placed->start)) -
                              placed->offset_frames ==
                          10240,
            "the take after the gap sounds from sample 10240");
}

// Scenario: Monitoring is heard but never bounced
void monitoring() {
    const auto dir = work_dir("monitoring");
    auto song = input_song(2);
    song.audio_files = {write_click(dir / "click.wav")};
    add_clip(song, 0, 0, click_tick, click_frames);
    song.tracks[1].input = {true, TrackInput::Source::audio, -1, 0, 2,
                            TrackInput::Monitor::automatic};
    SongEngine engine;
    AudioAssetCache cache;
    (void)build(engine, song, cache);
    RtAudioOutput output{RtAudioOutput::Mode::deterministic};
    output.model_input(2);
    require(output.open(engine) && output.start(), "the production output must start");
    const float centre = strip_gain(MixerStrip{}, false).left;
    const auto level = [](std::uint32_t channel, std::uint64_t) {
        return channel == 0 ? 0.3F : -0.2F;
    };
    const auto apply = [&] { engine.set_audio_inputs(audio_input_routes(song, 0)); };
    const auto heard = [&](float left, float right) {
        const auto out = pump(output, 1024, 1024, level);
        return std::abs(out.left.back() - left * centre) < 1e-6F &&
               std::abs(out.right.back() - right * centre) < 1e-6F;
    };
    // Stopped: an armed track on automatic monitoring is heard.
    apply();
    require(heard(0.3F, -0.2F), "armed, auto monitoring is heard on a stopped song");
    song.tracks[1].input.monitor = TrackInput::Monitor::off;
    apply();
    require(heard(0.0F, 0.0F), "with monitoring off nothing is heard");
    song.tracks[1].input.monitor = TrackInput::Monitor::automatic;
    song.tracks[1].input.armed = false;
    apply();
    require(heard(0.0F, 0.0F), "disarmed, automatic monitoring stops");
    song.tracks[1].input.monitor = TrackInput::Monitor::on;
    apply();
    require(heard(0.3F, -0.2F), "monitoring on is heard without an arm");
    // A mono input is heard on both sides.
    song.tracks[1].input.audio_channels = 1;
    apply();
    require(heard(0.3F, 0.3F), "a mono input is heard on both sides");
    song.tracks[1].input.audio_channels = 2;
    song.tracks[1].input.armed = true;
    song.tracks[1].input.monitor = TrackInput::Monitor::automatic;
    apply();
    // The strip applies: muted, the monitored input is silent.
    song.tracks[1].mix.mute = true;
    engine.apply_mix(song);
    require(heard(0.0F, 0.0F), "the track's strip applies to what it monitors");
    song.tracks[1].mix.mute = false;
    engine.apply_mix(song);

    // Playing and recording, the input is still heard over the song.
    TakeWriter writer;
    writer.begin(dir / "takes", static_cast<std::uint32_t>(rate));
    engine.connect_capture(&writer.ring());
    engine.set_recording(true);
    engine.set_playing(true);
    engine.seek(click_sample - 100);
    const auto live = pump(output, 1024, 1024, level);
    require(std::abs(live.left[100] - (click_level + 0.3F) * centre) < 1e-5F,
            "the click and the monitored input are heard together");

    // The bounce is the arrangement alone: the same file as a render of the
    // song through an engine with no input at all, sample for sample.
    std::string error;
    const auto bounce = dir / "bounce.wav";
    require(bounce_song(engine, bounce, WaveFormat::float32, 0, &error).has_value(),
            "bounce: " + error);
    const auto mix = read_back(bounce);
    SongEngine reference;
    AudioAssetCache fresh;
    (void)build(reference, song, fresh);
    reference.set_playing(true);
    std::vector<float> left(mix[0].size()), right(mix[0].size());
    for (std::size_t done = 0; done < left.size(); done += block) {
        const auto count = std::min<std::size_t>(block, left.size() - done);
        reference.process({std::span(left).subspan(done, count),
                           std::span(right).subspan(done, count)});
    }
    require(mix[0] == left && mix[1] == right,
            "the bounce equals the arrangement rendered with no input");
    require(probe::peak(mix[0]) > 0.5F, "and the arrangement is in it");
    // The bounce recorded nothing either: only the live block was captured.
    engine.set_playing(false);
    const auto takes = writer.finish();
    engine.connect_capture(nullptr);
    require(takes.size() == 1 && takes[0].frames == 1024,
            "only the live block was captured, not the bounce");
}

// Scenario: Takes are written as WAV files of the inputs they record
void take_files() {
    const auto dir = work_dir("take_files");
    auto song = input_song(2);
    song.clips = {{0, 0, 0, 1}};
    // A stereo pair from inputs 3-4, and input 2 alone.
    song.tracks[0].input = {true, TrackInput::Source::audio, -1, 2, 2, TrackInput::Monitor::off};
    song.tracks[1].input = {true, TrackInput::Source::audio, -1, 1, 1, TrackInput::Monitor::off};
    SongEngine engine;
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    RtAudioOutput output{RtAudioOutput::Mode::deterministic};
    output.model_input(4);
    require(output.open(engine) && output.start(), "the production output must start");
    const auto takes_dir = dir / "not-yet";
    TakeWriter writer;
    writer.begin(takes_dir, static_cast<std::uint32_t>(rate));
    require(!fs::exists(takes_dir), "no folder is made until a take needs one");
    engine.connect_capture(&writer.ring());
    engine.set_audio_inputs(audio_input_routes(song, 0));
    engine.set_recording(true);
    engine.set_playing(true);
    engine.seek(1000);
    const auto signal = [](std::uint32_t channel, std::uint64_t frame) {
        return static_cast<float>(std::sin(0.01 * static_cast<double>(frame) * (channel + 1))) *
               0.5F;
    };
    (void)pump(output, 3000, 333, signal);
    const auto takes = writer.finish();
    require(takes.size() == 2, "one take per recording track");
    std::map<std::uint32_t, RecordedTake> by_track;
    for (const auto& take : takes) by_track[take.track] = take;
    const auto& pair = by_track.at(0);
    const auto& mono = by_track.at(1);
    require(pair.first_sample == 1000 && pair.frames == 3000 && pair.channels == 2 &&
                mono.channels == 1 && mono.frames == 3000,
            "the takes start where recording did and have their tracks' channels");
    require(pair.file.parent_path() == takes_dir && fs::exists(pair.file) &&
                pair.file != mono.file,
            "each take is its own file in the take folder");
    const auto stereo = read_back(pair.file);
    const auto single = read_back(mono.file);
    require(stereo.size() == 2 && single.size() == 1, "a stereo file and a mono file");
    for (std::size_t frame = 0; frame < 3000; ++frame) {
        require(stereo[0][frame] == signal(2, frame) && stereo[1][frame] == signal(3, frame),
                "the pair's file holds inputs 3 and 4 as played");
        require(single[0][frame] == signal(1, frame), "the mono file holds input 2 as played");
    }
    // The take's audio for the asset store is the same audio as its file.
    require(pair.audio && pair.audio->frames == 3000 && pair.audio->left == stereo[0] &&
                pair.audio->right == stereo[1] && !pair.audio->peaks.empty(),
            "the take's decoded audio is its file's");
}

// Scenario: A device without inputs still plays
void output_only() {
    const auto dir = work_dir("output_only");
    auto song = input_song(2);
    song.audio_files = {write_click(dir / "click.wav")};
    add_clip(song, 0, 0, click_tick, click_frames);
    song.tracks[1].input = {true, TrackInput::Source::audio, -1, 0, 2, TrackInput::Monitor::on};
    SongEngine engine;
    AudioAssetCache cache;
    (void)build(engine, song, cache);
    RtAudioOutput output{RtAudioOutput::Mode::deterministic};
    require(output.open(engine) && output.start(), "the production output must start");
    require(output.input_channels() == 0 && output.device_info().input_channels == 0,
            "an output-only device reports no inputs");
    TakeWriter writer;
    writer.begin(dir / "takes", static_cast<std::uint32_t>(rate));
    engine.connect_capture(&writer.ring());
    engine.set_audio_inputs(audio_input_routes(song, 0));
    engine.set_recording(true);
    engine.set_playing(true);
    std::vector<float> stereo(2 * engine.song_samples());
    require(output.pump(stereo), "the song plays");
    const std::span<const float> left(stereo.data(), engine.song_samples());
    require(probe::first_nonzero(left, 0.01F) == click_sample, "the click plays on its sample");
    const auto takes = writer.finish();
    require(takes.empty() && !fs::exists(dir / "takes"),
            "without inputs nothing is recorded and no file is made");
}

const std::map<std::string, std::function<void()>> cases{
    {"routes", routes},
    {"loopback_click", loopback_click},
    {"latent_master_insert", latent_master_insert},
    {"overflow", overflow},
    {"monitoring", monitoring},
    {"take_files", take_files},
    {"output_only", output_only},
};

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "pass a case name");
        const auto found = cases.find(argv[1]);
        require(found != cases.end(), std::string("unknown case ") + argv[1]);
        found->second();
        std::cout << argv[1] << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << (argc > 1 ? argv[1] : "") << ": " << error.what() << '\n';
        return 1;
    }
}
