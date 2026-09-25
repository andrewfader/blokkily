// Clip warp (item 3.6), proved from rendered audio: renditions are rendered by
// Rubber Band on the production worker (WarpRenderer), played through the
// production render callback (the deterministic pump) and the production
// bounce, which is read back and compared with what the engine played.
//
// features/clip_warp.feature: each case below names its scenario.

#include "support/audio_probe.hpp"

#include "blokkily/audio/audio_asset.hpp"
#include "blokkily/audio/audio_clips.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/clip_warp.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/project/project.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace blokkily;
namespace fs = std::filesystem;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 512;
constexpr double millisecond = rate / 1000.0;

fs::path work_dir(const std::string& name) {
    const fs::path dir = fs::path(BLOKKILY_CLIP_WARP_WORK) / name;
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir);
    return dir;
}

AudioFileRef write_mono(const fs::path& file, std::uint64_t frames,
                        const std::function<float(std::uint64_t)>& content) {
    std::vector<float> samples(frames);
    for (std::uint64_t frame = 0; frame < frames; ++frame) samples[frame] = content(frame);
    WaveWriter writer;
    std::string error;
    require(writer.open(file, static_cast<std::uint32_t>(rate), WaveFormat::float32, &error) &&
                writer.write(samples, samples, &error) && writer.close(&error),
            "write " + file.string() + ": " + error);
    return {file, frames, static_cast<std::uint32_t>(rate), 2};
}

// A click track recorded at `bpm`: a 5 ms burst of 3 kHz on every beat.
AudioFileRef write_clicks(const fs::path& file, double bpm, int beats) {
    const double period = rate * 60.0 / bpm;
    const auto frames = static_cast<std::uint64_t>(std::llround(period * beats));
    return write_mono(file, frames, [period](std::uint64_t frame) {
        const double beat = std::floor(static_cast<double>(frame) / period);
        const double into = static_cast<double>(frame) - std::llround(beat * period);
        if (into < 0.0 || into >= 240.0) return 0.0F;
        return static_cast<float>(0.8 * std::sin(2.0 * std::numbers::pi * 3000.0 * into / rate) *
                                  std::exp(-into / 60.0));
    });
}

AudioFileRef write_tone(const fs::path& file, double hz, std::uint64_t frames) {
    return write_mono(file, frames, [hz](std::uint64_t frame) {
        return static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * hz *
                                                 static_cast<double>(frame) / rate));
    });
}

Song clip_song(std::size_t tracks = 1) {
    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks.assign(tracks, Track{});
    song.clips = {};
    return song;
}

AudioClip& add_clip(Song& song, std::size_t file, Tick start, std::size_t track = 0) {
    AudioClip clip;
    clip.id = song.next_audio_clip_id();
    clip.track = track;
    clip.file = file;
    clip.start = start;
    clip.length_frames = song.audio_files.at(file).frames;
    song.audio_clips.push_back(clip);
    return song.audio_clips.back();
}

// Every rendition the song's warped clips need, rendered by the production
// worker off this thread, the way the application gets them.
ClipRenditions render_renditions(const Song& song, const AudioAssets& assets,
                                 AudioAssetCache& cache) {
    const TickClock clock(song.tempo, song.ticks_per_beat(), rate);
    WarpRenderer renderer;
    std::vector<std::string> keys;
    for (const auto& clip : song.audio_clips) {
        const auto& source = assets.at(clip.file);
        require(source != nullptr, "the clip's file loads");
        auto plan = plan_clip_warp(song, clip, clock, source->frames);
        if (!plan) continue;
        keys.push_back(plan->key());
        renderer.submit(source, std::move(*plan));
    }
    renderer.wait_idle();
    ClipRenditions renditions;
    for (auto& done : renderer.take_finished()) {
        require(done.asset.has_value(), "the rendition renders: " + done.error);
        cache.insert_derived(done.key, std::make_shared<const AudioAsset>(std::move(*done.asset)));
    }
    for (const auto& key : keys) {
        auto asset = cache.derived(key);
        require(asset != nullptr, "every warped clip has its rendition in the cache");
        renditions[key] = asset;
    }
    return renditions;
}

struct Stereo {
    std::vector<float> left;
    std::vector<float> right;
};

struct Player {
    SongEngine engine;
    RtAudioOutput output{RtAudioOutput::Mode::deterministic};
    AudioAssetCache cache;
    AudioAssets assets;
    ClipRenditions renditions;

    void prepare(const Song& song, bool render = true) {
        ClipAssetReport report;
        assets = load_clip_assets(song, cache, rate, &report);
        require(report.missing_count() == 0, "every file loads");
        if (render) renditions = render_renditions(song, assets, cache);
        std::string error;
        require(engine.prepare(song, rate, block, 0, &error, assets, renditions),
                "prepare: " + error);
        require(output.open(engine) && output.start(), "the production output must start");
        engine.set_playing(true);
    }
    void recompile(const Song& song) {
        std::string error;
        require(engine.recompile(song, 0, &error, assets, renditions), "recompile: " + error);
    }
    Stereo pump(std::size_t frames) {
        std::vector<float> stereo(frames * 2, -9.0F);
        require(output.pump(stereo), "the production callback must render");
        return {{stereo.begin(), stereo.begin() + static_cast<std::ptrdiff_t>(frames)},
                {stereo.begin() + static_cast<std::ptrdiff_t>(frames), stereo.end()}};
    }
};

// Where each click starts: the first sample over `threshold`, then nothing
// for half a beat of the fastest tempo in play.
std::vector<std::size_t> onsets(std::span<const float> samples, float threshold = 0.1F) {
    std::vector<std::size_t> found;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (std::abs(samples[index]) <= threshold) continue;
        if (!found.empty() && index < found.back() + 9000) continue;
        found.push_back(index);
    }
    return found;
}

// The first and last samples louder than `threshold`.
std::pair<std::size_t, std::size_t> extent(std::span<const float> samples, float threshold) {
    std::size_t first = samples.size();
    std::size_t last = 0;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (std::abs(samples[index]) <= threshold) continue;
        first = std::min(first, index);
        last = index;
    }
    return {first, last};
}

// features/clip_warp.feature:
//   Scenario: A click track follows the song's tempo map, steps and ramps included
//   Scenario: A warped clip exports exactly as it plays
void follow_tempo() {
    const auto dir = work_dir("follow_tempo");
    auto song = clip_song();
    // 120 BPM; a step to 90 at beat 8; from beat 12 a ramp up to 140 at
    // beat 16, which then holds.
    song.tempo.set({3840, 90.0, false});
    song.tempo.set({5760, 90.0, true});
    song.tempo.set({7680, 140.0, false});
    constexpr int beats = 20;
    song.audio_files.push_back(write_clicks(dir / "clicks100.wav", 100.0, beats));
    auto& clip = add_clip(song, 0, 480);   // the recording's first beat on the song's second
    clip.warp.follow_tempo = true;
    clip.warp.source_bpm = 100.0;
    require(song.consistent(), "the warped song is consistent");
    require(song.length() == 480 + beats * 480,
            "a clip following the tempo lasts its beats: " + std::to_string(song.length()));

    // Exported first, then compared with what the engine plays.
    Player exporter;
    exporter.prepare(song);
    exporter.engine.set_playing(false);
    std::string error;
    const auto file = dir / "bounce.wav";
    require(bounce_song(exporter.engine, file, WaveFormat::float32, 0, &error).has_value(),
            "bounce: " + error);
    const auto read = read_wave(file, &error);
    const auto samples = exporter.engine.song_samples();
    require(read.has_value() && read->channels == 2 && read->frames == samples,
            "the bounce reads back with the song's length: " + error);
    std::vector<float> left(samples);
    for (std::uint64_t frame = 0; frame < samples; ++frame)
        left[frame] = read->interleaved[frame * 2];

    // Every click of the recording on its beat of the song, within 1 ms.
    const TickClock clock(song.tempo, song.ticks_per_beat(), rate);
    const auto heard = onsets(left);
    require(heard.size() == static_cast<std::size_t>(beats),
            "every click is heard once: " + std::to_string(heard.size()));
    double worst = 0.0;
    for (int beat = 0; beat < beats; ++beat) {
        const double expected = clock.sample_at(480 + beat * 480);
        const double error_ms = (static_cast<double>(heard[beat]) - expected) / millisecond;
        std::cout << "beat " << beat + 1 << ": " << error_ms << " ms\n";
        worst = std::max(worst, std::abs(error_ms));
        require(std::abs(error_ms) <= 1.0,
                "click " + std::to_string(beat) + " is " + std::to_string(error_ms) +
                    " ms off its beat");
    }
    std::cout << "worst click " << worst << " ms off its beat\n";

    // The live render is the export, sample for sample.
    Player live;
    live.prepare(song);
    const auto played = live.pump(samples);
    for (std::uint64_t frame = 0; frame < samples; ++frame)
        require(read->interleaved[frame * 2] == played.left[frame] &&
                    read->interleaved[frame * 2 + 1] == played.right[frame],
                "bounce frame " + std::to_string(frame) + " is what the engine played");
}

// features/clip_warp.feature:
//   Scenario: A click track at 100 BPM lands on a 120 BPM song's beats
void follow_constant() {
    const auto dir = work_dir("follow_constant");
    auto song = clip_song();
    constexpr int beats = 16;
    song.audio_files.push_back(write_clicks(dir / "clicks100.wav", 100.0, beats));
    auto& clip = add_clip(song, 0, 0);
    clip.warp.follow_tempo = true;
    clip.warp.source_bpm = 100.0;
    Player player;
    player.prepare(song);
    const auto heard = player.pump(player.engine.song_samples());
    const auto found = onsets(heard.left);
    require(found.size() == beats, "every click is heard once");
    for (int beat = 0; beat < beats; ++beat) {
        const double error_ms = (static_cast<double>(found[beat]) - beat * 24000.0) / millisecond;
        require(std::abs(error_ms) <= 1.0, "click " + std::to_string(beat) + " is " +
                                               std::to_string(error_ms) + " ms off");
    }
    // Unwarped, the same file drifts: 0.6 s a beat against the song's 0.5 s
    // puts its fourth click 0.3 s (14400 samples) late.
    auto plain = song;
    plain.audio_clips[0].warp = ClipWarp{};
    Player unwarped;
    unwarped.prepare(plain);
    const auto late = onsets(unwarped.pump(96000).left);
    require(late.size() >= 4 && late[3] >= 3 * 24000 + 14000,
            "without warp the clicks drift off the song's beats");
}

// The song bounced with `tail` frames after its end, read back: left channel.
std::vector<float> bounced_left(Player& player, const fs::path& file, std::uint64_t tail) {
    player.engine.set_playing(false);
    std::string error;
    require(bounce_song(player.engine, file, WaveFormat::float32, tail, &error).has_value(),
            "bounce: " + error);
    const auto read = read_wave(file, &error);
    require(read.has_value() && read->channels == 2, "the bounce reads back: " + error);
    std::vector<float> left(read->frames);
    for (std::uint64_t frame = 0; frame < read->frames; ++frame)
        left[frame] = read->interleaved[frame * 2];
    return left;
}

// features/clip_warp.feature:
//   Scenario: A pitch shift of +12 semitones plays an octave up at the same length
void pitch_octave() {
    const auto dir = work_dir("pitch_octave");
    auto song = clip_song();
    constexpr std::uint64_t frames = 96000;   // two seconds, four beats at 120
    song.audio_files.push_back(write_tone(dir / "tone1k.wav", 1000.0, frames));
    add_clip(song, 0, 0).warp.semitones = 12;
    Player player;
    player.prepare(song);
    require(player.engine.song_samples() == frames, "the song lasts the clip's two seconds");
    // Played through the callback, and exported with half a second after it.
    const auto heard = player.pump(frames);
    const auto exported = bounced_left(player, work_dir("pitch_octave_bounce") / "bounce.wav",
                                       24000);
    require(exported.size() == frames + 24000 &&
                std::equal(heard.left.begin(), heard.left.end(), exported.begin()),
            "the export is what was played");
    const std::span<const float> middle{heard.left.data() + 24000, 48000};
    const double hz = probe::dominant_frequency(middle, rate, 500.0, 3000.0, 1.0);
    std::cout << "shifted tone at " << hz << " Hz\n";
    require(std::abs(hz - 2000.0) <= 2.0, "the tone sounds at 2 kHz: " + std::to_string(hz));
    require(probe::goertzel_energy(middle, rate, 2000.0) >
                100.0 * probe::goertzel_energy(middle, rate, 1000.0),
            "nothing is left at 1 kHz");
    // The same two seconds: sound from the first frame to the last, then none.
    const auto [first, last] = extent(exported, 0.05F);
    std::cout << "shifted tone sounds from " << first << " to " << last << '\n';
    require(first < 5 * millisecond && last + 1 >= frames - 5 * millisecond && last < frames,
            "the shifted clip lasts as long as the file");
    require(probe::rms({heard.left.data() + frames - 4800, 4800}) > 0.2,
            "it is at full level up to its end");
}

// features/clip_warp.feature:
//   Scenario: A stretch ratio of 2 doubles a clip's length and keeps its pitch
void ratio_double() {
    const auto dir = work_dir("ratio_double");
    auto song = clip_song();
    constexpr std::uint64_t frames = 48000;
    song.audio_files.push_back(write_tone(dir / "tone1k.wav", 1000.0, frames));
    add_clip(song, 0, 0).warp.ratio = 2.0;
    require(song.length() == 1920, "twice one second is two seconds: a bar at 120 BPM");
    Player player;
    player.prepare(song);
    require(player.engine.song_samples() == 2 * frames, "the song lasts the stretched clip");
    const auto heard = player.pump(2 * frames);
    const auto exported = bounced_left(player, work_dir("ratio_double_bounce") / "bounce.wav",
                                       24000);
    require(exported.size() == 2 * frames + 24000 &&
                std::equal(heard.left.begin(), heard.left.end(), exported.begin()),
            "the export is what was played");
    const auto [first, last] = extent(exported, 0.05F);
    std::cout << "stretched tone sounds from " << first << " to " << last << '\n';
    require(first < 5 * millisecond && last + 1 >= 2 * frames - 5 * millisecond &&
                last < 2 * frames,
            "the stretched clip lasts two seconds");
    const std::span<const float> middle{heard.left.data() + 24000, 48000};
    const double hz = probe::dominant_frequency(middle, rate, 500.0, 3000.0, 1.0);
    std::cout << "stretched tone at " << hz << " Hz\n";
    require(std::abs(hz - 1000.0) <= 2.0, "the pitch stays at 1 kHz: " + std::to_string(hz));
}

// features/clip_warp.feature:
//   Scenario: A clip is silent while its rendition renders, and never plays stale
void pending_silent() {
    const auto dir = work_dir("pending_silent");
    auto song = clip_song(2);
    song.audio_files.push_back(write_tone(dir / "tone1k.wav", 1000.0, 48000));
    add_clip(song, 0, 0, 0).warp.semitones = 12;
    add_clip(song, 0, 0, 1);   // the same file unwarped on the other track
    song.tracks[0].mix.pan = -1.0;
    song.tracks[1].mix.pan = 1.0;
    Player player;
    player.prepare(song, false);   // no renditions yet
    auto heard = player.pump(24000);
    // Hard right leaves the left channel at the pan law's cos(pi/2), not 0.
    require(probe::peak(heard.left) < 1e-6F, "the warped clip is silent while it renders");
    require(probe::peak(heard.right) > 0.2F, "the unwarped clip plays meanwhile");

    // The rendition arrives: the next recompile plays it, from where the
    // playhead is, without a rebuild.
    player.renditions = render_renditions(song, player.assets, player.cache);
    player.recompile(song);
    heard = player.pump(12000);
    require(probe::rms({heard.left.data() + 2048, 8192}) > 0.2, "the rendition swaps in");
    require(std::abs(probe::dominant_frequency({heard.left.data() + 2048, 8192}, rate, 500.0,
                                               3000.0, 5.0) - 2000.0) <= 10.0,
            "and it is the octave up");

    // An edit makes that rendition stale: silent again, not the old sound.
    auto edited = song;
    edited.audio_clips[0].warp.semitones = 7;
    player.recompile(edited);
    heard = player.pump(8192);
    require(probe::peak({heard.left.data() + 2048, 4096}) < 1e-6F,
            "a stale rendition is never played");
}

// features/clip_warp.feature:
//   Scenario: A clip's warp survives a save and a load
void project_round_trip() {
    const auto dir = work_dir("project_round_trip");
    Project project;
    auto& song = project.song;
    song = clip_song();
    song.audio_files.push_back(write_tone(dir / "tone.wav", 440.0, 4800));
    auto& follow = add_clip(song, 0, 0);
    follow.warp.follow_tempo = true;
    follow.warp.source_bpm = 97.5;
    add_clip(song, 0, 960);   // left alone: no record
    auto& shifted = add_clip(song, 0, 1920);
    shifted.warp.ratio = 1.5;
    shifted.warp.semitones = -3;
    shifted.warp.cents = 25.5;
    const auto text = ProjectFile::serialize(project, dir);
    require(text.find("clipwarp 1 1 97.5 1 0 0\n") != std::string::npos, "follow is written");
    require(text.find("clipwarp 3 0 0 1.5 -3 25.5\n") != std::string::npos,
            "ratio and pitch are written");
    require(text.find("clipwarp 2 ") == std::string::npos, "an unwarped clip writes nothing");
    std::string error;
    const auto back = ProjectFile::parse(text, &error, dir);
    require(back.has_value(), "the warped project loads: " + error);
    require(back->song.audio_clips == song.audio_clips, "every clip comes back with its warp");
    require(ProjectFile::serialize(*back, dir) == text, "saving again writes the same bytes");

    const auto refuses = [&](const std::string& from, const std::string& to) {
        auto broken = text;
        const auto at = broken.find(from);
        require(at != std::string::npos, "the test edits a line that exists");
        broken.replace(at, from.size(), to);
        std::string why;
        const bool loaded = ProjectFile::parse(broken, &why, dir).has_value();
        require(!loaded && !why.empty(), "a bad clipwarp is refused: " + to);
    };
    refuses("clipwarp 1 1 97.5 1 0 0", "clipwarp 9 1 97.5 1 0 0");      // no such clip
    refuses("clipwarp 1 1 97.5 1 0 0", "clipwarp 1 2 97.5 1 0 0");      // follow is 0 or 1
    refuses("clipwarp 1 1 97.5 1 0 0", "clipwarp 1 1 97.5 9 0 0");      // ratio out of range
    refuses("clipwarp 1 1 97.5 1 0 0", "clipwarp 1 1 97.5 1 0");        // a field short
    refuses("clipwarp 1 1 97.5 1 0 0", "clipwarp 1 1 97.5 1 30 0");     // semitones out of range
    refuses("clipwarp 1 1 97.5 1 0 0", "clipwarp 1 0 0 1 0 0");         // changes nothing
    refuses("clipwarp 3 0 0 1.5 -3 25.5", "clipwarp 1 0 0 1.5 -3 25.5"); // twice for clip 1
}

// features/clip_warp.feature:
//   Scenario: A clip's source tempo is detected from its audio
void detect_bpm() {
    const auto dir = work_dir("detect_bpm");
    for (const double bpm : {100.0, 128.0, 87.0}) {
        const auto ref = write_clicks(dir / "clicks.wav", bpm, 16);
        AudioAssetCache cache;
        const auto asset = cache.load(ref.path, rate, std::nullopt);
        require(asset != nullptr, "the click track loads");
        const auto found = detect_tempo(*asset, 0, asset->frames);
        std::cout << bpm << " BPM detected as " << (found ? *found : 0.0) << '\n';
        require(found.has_value() && *found == bpm,
                "the click track's tempo is found: " + std::to_string(bpm));
    }
    // Silence has no tempo.
    const auto quiet = write_mono(dir / "quiet.wav", 96000, [](std::uint64_t) { return 0.0F; });
    AudioAssetCache cache;
    const auto asset = cache.load(quiet.path, rate, std::nullopt);
    require(asset != nullptr && !detect_tempo(*asset, 0, asset->frames).has_value(),
            "silence has no tempo");
}

// features/clip_warp.feature:
//   Scenario: A rendition is kept in the asset cache and let go when unused
void cache_derived() {
    const auto dir = work_dir("cache_derived");
    auto song = clip_song();
    song.audio_files.push_back(write_tone(dir / "tone.wav", 500.0, 24000));
    add_clip(song, 0, 0).warp.ratio = 1.25;
    AudioAssetCache cache;
    const auto assets = load_clip_assets(song, cache, rate);
    const TickClock clock(song.tempo, song.ticks_per_beat(), rate);
    const auto plan = plan_clip_warp(song, song.audio_clips[0], clock, assets[0]->frames);
    require(plan.has_value() && plan->output_frames == 30000, "a 1.25 stretch is 30000 frames");
    // The key names the file, the map, the pitch and the rate.
    auto shifted = song;
    shifted.audio_clips[0].warp.cents = 10.0;
    const TickClock other_rate(song.tempo, song.ticks_per_beat(), 44100.0);
    require(plan_clip_warp(shifted, shifted.audio_clips[0], clock, assets[0]->frames)->key() !=
                    plan->key() &&
                plan_clip_warp(song, song.audio_clips[0], other_rate, 22050)->key() != plan->key(),
            "pitch and rate are part of the key");
    auto renditions = render_renditions(song, assets, cache);
    require(cache.derived_count() == 1 && cache.derived(plan->key()) != nullptr,
            "the rendition is in the cache under its key");
    cache.purge_unused();
    require(cache.derived_count() == 1, "a rendition still played is kept");
    renditions.clear();
    cache.purge_unused();
    require(cache.derived_count() == 0, "an unused rendition is let go");
}

} // namespace

int main(int argc, char** argv) {
    const std::map<std::string, void (*)()> cases{
        {"follow_tempo", follow_tempo},
        {"follow_constant", follow_constant},
        {"pitch_octave", pitch_octave},
        {"ratio_double", ratio_double},
        {"pending_silent", pending_silent},
        {"project_round_trip", project_round_trip},
        {"detect_bpm", detect_bpm},
        {"cache_derived", cache_derived},
    };
    if (argc != 2 || !cases.contains(argv[1])) {
        std::cerr << "usage: blokkily_clip_warp_tests <case>\n";
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
