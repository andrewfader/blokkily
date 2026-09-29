// Disk streaming (wave 4.2), proved from rendered audio through the
// production render callback (the deterministic pump) and the production
// bounce, on real files: a clip whose file is over the streaming threshold
// plays from disk and sounds exactly as the same clip played from memory.
//
// The engines here run with no background worker; the test "is the disk"
// and fills the rings (SongEngine::service_disk_streams) when it decides to,
// so starvation is reproduced exactly rather than hoped for.
//
// features/disk_streaming.feature: each case names its scenario.

#include "support/audio_probe.hpp"

#include "blokkily/audio/audio_asset.hpp"
#include "blokkily/audio/audio_clips.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/disk_stream.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;
namespace fs = std::filesystem;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 512;

fs::path work_dir(const std::string& name) {
    const fs::path dir = fs::path(BLOKKILY_STREAM_WORK) / name;
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir);
    return dir;
}

// Two steady tones, one per side: smooth, so any step in the output larger
// than the tone's own is a click the stream made.
float tone_left(std::uint64_t frame, double file_rate) {
    return 0.5F * static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 *
                                              static_cast<double>(frame) / file_rate));
}
float tone_right(std::uint64_t frame, double file_rate) {
    return 0.4F * static_cast<float>(std::sin(2.0 * std::numbers::pi * 660.0 *
                                              static_cast<double>(frame) / file_rate));
}

AudioFileRef write_tones(const fs::path& file, std::uint64_t frames, std::uint32_t file_rate) {
    std::vector<float> l(frames), r(frames);
    for (std::uint64_t i = 0; i < frames; ++i) {
        l[i] = tone_left(i, file_rate);
        r[i] = tone_right(i, file_rate);
    }
    WaveWriter writer;
    std::string error;
    require(writer.open(file, file_rate, WaveFormat::float32, &error) &&
                writer.write(l, r, &error) && writer.close(&error),
            "write " + file.string() + ": " + error);
    return {file, frames, file_rate, 2};
}

Song clip_song() {
    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks = {Track{}};
    song.clips = {};
    return song;
}

AudioClip& add_clip(Song& song, std::size_t file, Tick start, std::uint64_t offset,
                    std::uint64_t length) {
    AudioClip clip;
    clip.id = song.next_audio_clip_id();
    clip.track = 0;
    clip.file = file;
    clip.start = start;
    clip.offset_frames = offset;
    clip.length_frames = length;
    song.audio_clips.push_back(clip);
    return song.audio_clips.back();
}

struct Stereo {
    std::vector<float> left;
    std::vector<float> right;
    void append(const Stereo& more) {
        left.insert(left.end(), more.left.begin(), more.left.end());
        right.insert(right.end(), more.right.begin(), more.right.end());
    }
};

// The production callback, pumped a block at a time. `stream` sets the
// threshold to nothing, so every clip streams; otherwise nothing does.
struct Player {
    SongEngine engine;
    RtAudioOutput output{RtAudioOutput::Mode::deterministic};
    AudioAssetCache cache;
    AudioAssets assets;
    ClipAssetReport report;

    Player(const Song& song, bool stream, std::size_t workers = 0) {
        cache.set_stream_threshold(stream ? 0 : std::numeric_limits<std::uint64_t>::max());
        assets = load_clip_assets(song, cache, rate, &report);
        require(report.missing_count() == 0, "every file loads");
        for (const auto& asset : assets)
            require(asset && asset->is_streamed() == stream,
                    stream ? "an asset over the threshold streams"
                           : "an asset under the threshold is in memory");
        engine.set_disk_stream_workers(workers);
        std::string error;
        require(engine.prepare(song, rate, block, 0, &error, assets), "prepare: " + error);
        require(output.open(engine) && output.start(), "the production output must start");
    }
    // `serve`: the disk answers before every block.
    Stereo pump(std::size_t frames, bool serve = true) {
        Stereo heard;
        for (std::size_t done = 0; done < frames; done += block) {
            const auto count = std::min<std::size_t>(block, frames - done);
            if (serve) (void)engine.service_disk_streams();
            std::vector<float> stereo(count * 2, -9.0F);
            require(output.pump(stereo), "the production callback must render");
            heard.left.insert(heard.left.end(), stereo.begin(),
                              stereo.begin() + static_cast<std::ptrdiff_t>(count));
            heard.right.insert(heard.right.end(),
                               stereo.begin() + static_cast<std::ptrdiff_t>(count), stereo.end());
        }
        return heard;
    }
};

void require_equal(const Stereo& got, const Stereo& expected, std::size_t from, std::size_t to,
                   float tolerance, const std::string& what) {
    for (std::size_t i = from; i < to; ++i) {
        const float dl = std::abs(got.left[i] - expected.left[i]);
        const float dr = std::abs(got.right[i] - expected.right[i]);
        require(dl <= tolerance && dr <= tolerance,
                what + ": sample " + std::to_string(i) + " is " + std::to_string(got.left[i]) +
                    " / " + std::to_string(got.right[i]) + ", expected " +
                    std::to_string(expected.left[i]) + " / " + std::to_string(expected.right[i]));
    }
}

// The largest step between neighbouring samples of either channel.
float largest_step(const Stereo& heard, std::size_t from, std::size_t to) {
    float step = 0.0F;
    for (std::size_t i = std::max<std::size_t>(from, 1); i < to; ++i)
        step = std::max({step, std::abs(heard.left[i] - heard.left[i - 1]),
                         std::abs(heard.right[i] - heard.right[i - 1])});
    return step;
}

// The tones' own steepest step at 48 kHz, plus what a 128-sample fade of the
// louder one adds to it: a click from a cut would be up to half of full scale.
const float smooth_step = static_cast<float>(0.5 * 2.0 * std::numbers::pi * 660.0 / rate) +
                          0.5F / static_cast<float>(DiskStream::fade_frames) + 1e-4F;

// features/disk_streaming.feature:
//   Scenario: A long clip streams from disk and sounds as it would from memory
void playback() {
    const auto dir = work_dir("playback");
    auto song = clip_song();
    song.audio_files.push_back(write_tones(dir / "long.wav", 4 * 48000, 48000));
    auto& clip = add_clip(song, 0, 480, 3001, 150000);
    clip.gain_db = -4.5;
    clip.fade_in_frames = 700;
    clip.fade_out_frames = 2400;

    Player memory(song, false);
    Player streamed(song, true);
    require(streamed.assets[0]->left.empty() && streamed.assets[0]->right.empty(),
            "a streamed asset holds no samples");
    require(streamed.assets[0]->frames == memory.assets[0]->frames,
            "a streamed asset has the frames the decoded one has");
    require(streamed.assets[0]->peaks == memory.assets[0]->peaks,
            "and the same overview, read through the stream");
    memory.engine.set_playing(true);
    streamed.engine.set_playing(true);
    const auto frames = static_cast<std::size_t>(memory.engine.song_samples());
    const auto expected = memory.pump(frames);
    const auto heard = streamed.pump(frames);
    require(probe::peak(expected.left) > 0.2F, "the clip sounds");
    require_equal(heard, expected, 0, frames, 0.0F, "streamed clip against the same clip in memory");

    // A load that needs the samples still gets them.
    std::string error;
    const auto& file = song.audio_files[0];
    const auto decoded = streamed.cache.load(file.path, rate,
                                             AudioFileInfo{file.frames, file.sample_rate, file.channels},
                                             &error, AudioAssetCache::Residency::memory);
    require(decoded && !decoded->is_streamed() && decoded->left.size() == decoded->frames,
            "a memory load of a streamed file decodes it: " + error);
}

// features/disk_streaming.feature:
//   Scenario: A streamed file at another rate is resampled as it is in memory
void resampled() {
    const auto dir = work_dir("resampled");
    auto song = clip_song();
    song.audio_files.push_back(write_tones(dir / "long44.wav", 5 * 44100, 44100));
    add_clip(song, 0, 0, 1234, 4 * 44100);

    // The reader itself, anywhere in the file, against resample().
    const auto decoded = decode_audio_file(song.audio_files[0].path);
    require(decoded.has_value(), "the file decodes");
    const auto ratio = rate / 44100.0;
    const auto left = resample(decoded->left, ratio);
    const auto right = resample(decoded->right, ratio);
    StreamReader reader;
    require(reader.open(song.audio_files[0].path, rate), "the reader opens");
    require(reader.frames() == left.size(), "the reader has resample()'s frame count");
    std::vector<float> l(4000), r(4000);
    for (const std::uint64_t at : {std::uint64_t{0}, std::uint64_t{777}, std::uint64_t{50001},
                                   std::uint64_t{123457}, left.size() - 1000}) {
        reader.seek(at);
        reader.read(l.data(), r.data(), l.size());
        for (std::size_t i = 0; i < l.size(); ++i) {
            const auto frame = at + i;
            const float want_l = frame < left.size() ? left[frame] : 0.0F;
            const float want_r = frame < right.size() ? right[frame] : 0.0F;
            require(std::abs(l[i] - want_l) <= 2e-5F && std::abs(r[i] - want_r) <= 2e-5F,
                    "resampled frame " + std::to_string(frame) + " after a seek to " +
                        std::to_string(at) + ": " + std::to_string(l[i]) + " vs " +
                        std::to_string(want_l));
        }
    }

    Player memory(song, false);
    Player streamed(song, true);
    memory.engine.set_playing(true);
    streamed.engine.set_playing(true);
    const auto frames = static_cast<std::size_t>(memory.engine.song_samples());
    const auto expected = memory.pump(frames);
    const auto heard = streamed.pump(frames);
    require_equal(heard, expected, 0, frames, 2e-5F, "a resampled clip streamed");
    const double frequency =
        probe::dominant_frequency({heard.left.data() + 48000, 48000}, rate, 400.0, 480.0, 0.5);
    require(std::abs(frequency - 440.0) <= 1.0, "it keeps its pitch: " + std::to_string(frequency));
}

// features/disk_streaming.feature:
//   Scenario: Locating inside a streamed clip resumes there without a click
void seek() {
    const auto dir = work_dir("seek");
    auto song = clip_song();
    song.audio_files.push_back(write_tones(dir / "long.wav", 6 * 48000, 48000));
    add_clip(song, 0, 0, 0, 6 * 48000);

    Player memory(song, false);
    Player streamed(song, true);
    streamed.engine.set_playing(true);
    Stereo heard = streamed.pump(8 * block);
    // The playhead jumps; the disk has not answered yet when the next blocks
    // render, and then does.
    const std::uint64_t target = 200000;
    streamed.engine.seek(target);
    const auto jump = heard.left.size();
    heard.append(streamed.pump(3 * block, false));
    heard.append(streamed.pump(12 * block));

    require(largest_step(heard, 1, heard.left.size()) <= smooth_step,
            "no step larger than the tone's own and a short fade: " +
                std::to_string(largest_step(heard, 1, heard.left.size())));
    require(heard.left[jump] != 0.0F && heard.left[jump + DiskStream::fade_frames] == 0.0F,
            "what played fades out over the audio that followed it, then rests");
    // Once the fade-in is over it plays the file at the new position,
    // exactly as memory does.
    memory.engine.seek(target);
    memory.engine.set_playing(true);
    const auto after = memory.pump(15 * block);
    const std::size_t settled = 5 * block;
    for (std::size_t i = settled; i < after.left.size(); ++i)
        require(heard.left[jump + i] == after.left[i] && heard.right[jump + i] == after.right[i],
                "after the locate the stream plays song sample " + std::to_string(target + i));
    // It was silent while it waited, not playing the wrong place.
    require(probe::peak({heard.left.data() + jump + DiskStream::fade_frames,
                         3 * block - DiskStream::fade_frames}) == 0.0F,
            "while the disk has not answered the clip is silent");
}

// features/disk_streaming.feature:
//   Scenario: A stream the disk cannot keep up with dips and recovers without clicks
void underrun() {
    const auto dir = work_dir("underrun");
    auto song = clip_song();
    song.audio_files.push_back(write_tones(dir / "long.wav", 4 * 48000, 48000));
    add_clip(song, 0, 0, 0, 4 * 48000);

    Player memory(song, false);
    Player streamed(song, true);
    memory.engine.set_playing(true);
    streamed.engine.set_playing(true);
    const std::size_t blocks = 300;
    const auto expected = memory.pump(blocks * block);

    // The disk answers for a while, then stops for long enough that the ring
    // runs dry (65536 frames is 128 blocks), then answers again.
    Stereo heard = streamed.pump(20 * block);
    heard.append(streamed.pump(160 * block, false));
    require(streamed.engine.service_disk_streams(), "the disk answers again");
    heard.append(streamed.pump((blocks - 180) * block));

    const auto silent_from = std::find_if(heard.left.begin() + 20 * block, heard.left.end(),
                                          [](float s) { return s == 0.0F; });
    require(silent_from != heard.left.end(), "a dry ring goes quiet");
    const auto dry = static_cast<std::size_t>(silent_from - heard.left.begin());
    require(dry > 128 * block, "only once what was buffered has played");
    require(largest_step(heard, 1, heard.left.size()) <= smooth_step,
            "fading out when dry and in on recovery, never a click: " +
                std::to_string(largest_step(heard, 1, heard.left.size())));
    // The fade out plays the audio still in the reserve, at a falling level.
    require(std::abs(heard.left[dry - 60]) > 0.0F || std::abs(heard.left[dry - 61]) > 0.0F,
            "the fade out is audio, not a cut");
    // Everything played before it ran dry, and everything after the fade-in,
    // is the file at the song's position: the stream stayed in time.
    require_equal(heard, expected, 0, dry - DiskStream::fade_frames, 0.0F, "before the dip");
    require_equal(heard, expected, 185 * block, blocks * block, 0.0F, "after recovery");
}

// features/disk_streaming.feature:
//   Scenario: A clip at the start of the song is ready again when the song wraps
void loop_cue() {
    const auto dir = work_dir("loop_cue");
    auto song = clip_song();
    song.audio_files.push_back(write_tones(dir / "long.wav", 3 * 48000, 48000));
    // One clip on the downbeat and one near the end, so the song wraps from
    // the second into the first. (A single clip spanning the wrap point
    // relocates like a locate: one stream per clip cannot be in two places.)
    add_clip(song, 0, 0, 24000, 24000);
    add_clip(song, 0, 1920, 60000, 12000);
    Player streamed(song, true);
    streamed.engine.set_playing(true);
    const auto length = static_cast<std::size_t>(streamed.engine.song_samples());
    const auto first = streamed.pump(length);
    const auto second = streamed.pump(length);
    require(probe::peak(first.left) > 0.3F, "the clip plays");
    require_equal(second, first, 0, length, 0.0F,
                  "the second pass is the first: the clip was cued before the wrap");
}

// features/disk_streaming.feature:
//   Scenario: An export of a streamed clip is the mix the engine plays
void bounce() {
    const auto dir = work_dir("bounce");
    auto song = clip_song();
    song.audio_files.push_back(write_tones(dir / "long.wav", 4 * 48000, 48000));
    song.audio_files.push_back(write_tones(dir / "long44.wav", 3 * 44100, 44100));
    add_clip(song, 0, 0, 100, 100000).fade_out_frames = 5000;
    add_clip(song, 1, 960, 0, 2 * 44100).gain_db = -6.0;

    Player memory(song, false);
    memory.engine.set_playing(true);
    const auto frames = static_cast<std::size_t>(memory.engine.song_samples());
    const auto expected = memory.pump(frames);
    // A real worker thread; the bounce must not depend on it keeping up.
    Player exporter(song, true, 1);
    exporter.engine.set_playing(true);
    (void)exporter.pump(3000);   // play somewhere first, so the bounce must relocate
    exporter.engine.set_playing(false);
    std::string error;
    const auto file = dir / "bounce.wav";
    const auto report = bounce_song(exporter.engine, file, WaveFormat::float32, 0, &error);
    require(report.has_value(), "bounce: " + error);
    require(!exporter.engine.blocking_disk_reads(), "the bounce puts live reads back");
    const auto read = read_wave(file, &error);
    require(read.has_value() && read->channels == 2 && read->frames == frames,
            "the export reads back with the song's length: " + error);
    for (std::size_t frame = 0; frame < frames; ++frame)
        require(std::abs(read->interleaved[frame * 2] - expected.left[frame]) <= 2e-5F &&
                    std::abs(read->interleaved[frame * 2 + 1] - expected.right[frame]) <= 2e-5F,
                "export frame " + std::to_string(frame) + " is what the engine plays");
    require(probe::peak(expected.left) > 0.2F, "the export is not silence");

    // And the live streamed render of the same song is that export too.
    Player live(song, true);
    live.engine.set_playing(true);
    const auto heard = live.pump(frames);
    for (std::size_t frame = 0; frame < frames; ++frame)
        require(read->interleaved[frame * 2] == heard.left[frame] &&
                    read->interleaved[frame * 2 + 1] == heard.right[frame],
                "export frame " + std::to_string(frame) + " is the streamed playback");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: blokkily_disk_streaming_tests <case>\n";
        return 2;
    }
    const std::map<std::string, std::function<void()>> cases{
        {"playback", playback}, {"resampled", resampled}, {"seek", seek},
        {"underrun", underrun}, {"loop_cue", loop_cue},   {"bounce", bounce}};
    const auto found = cases.find(argv[1]);
    if (found == cases.end()) {
        std::cerr << "Unknown case: " << argv[1] << "\n";
        return 2;
    }
    try {
        found->second();
    } catch (const std::exception& ex) {
        std::cerr << "FAILED: " << ex.what() << "\n";
        return 1;
    }
    std::cout << argv[1] << " passed\n";
    return 0;
}
