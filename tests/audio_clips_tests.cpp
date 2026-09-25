// Audio clips (item 2.2), proved from rendered audio through the production
// render callback (the deterministic pump) and the production bounce, on real
// files decoded by libsndfile.
//
// features/audio_clips.feature: each case below names its scenario.

#include "support/audio_probe.hpp"

#include "blokkily/audio/audio_asset.hpp"
#include "blokkily/audio/audio_clips.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/project/project.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
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
constexpr std::uint64_t ramp_frames = 24000;

// The ramp file: every frame a different value, so a sample heard names the
// frame of the file it came from. Left rises, right falls.
float ramp_left(std::uint64_t frame) { return static_cast<float>(frame + 1) / 65536.0F; }
float ramp_right(std::uint64_t frame) { return -static_cast<float>(frame + 1) / 65536.0F; }

fs::path work_dir(const std::string& name) {
    const fs::path dir = fs::path(BLOKKILY_AUDIO_CLIP_WORK) / name;
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir);
    return dir;
}

void write_stereo(const fs::path& file, std::uint64_t frames,
                  const std::function<float(std::uint64_t)>& left,
                  const std::function<float(std::uint64_t)>& right) {
    std::vector<float> l(frames), r(frames);
    for (std::uint64_t frame = 0; frame < frames; ++frame) {
        l[frame] = left(frame);
        r[frame] = right(frame);
    }
    WaveWriter writer;
    std::string error;
    fs::create_directories(file.parent_path());
    require(writer.open(file, static_cast<std::uint32_t>(rate), WaveFormat::float32, &error) &&
                writer.write(l, r, &error) && writer.close(&error),
            "write " + file.string() + ": " + error);
}

AudioFileRef write_ramp(const fs::path& file, std::uint64_t frames = ramp_frames) {
    write_stereo(file, frames, ramp_left, ramp_right);
    return {file, frames, static_cast<std::uint32_t>(rate), 2};
}

// A song with `tracks` instrument-less tracks and one empty pattern; the audio
// clips decide how long it is.
Song clip_song(std::size_t tracks = 1) {
    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks.assign(tracks, Track{});
    song.clips = {};
    return song;
}

AudioClip& add_clip(Song& song, std::size_t file, Tick start, std::uint64_t offset,
                    std::uint64_t length, std::size_t track = 0) {
    AudioClip clip;
    clip.id = song.next_audio_clip_id();
    clip.track = track;
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
};

// Renders `frames` through the production callback, from the song's start.
struct Player {
    SongEngine engine;
    RtAudioOutput output{RtAudioOutput::Mode::deterministic};
    AudioAssetCache cache;
    AudioAssets assets;
    ClipAssetReport report;

    void prepare(const Song& song) {
        assets = load_clip_assets(song, cache, rate, &report);
        std::string error;
        require(engine.prepare(song, rate, block, 0, &error, assets), "prepare: " + error);
        require(output.open(engine) && output.start(), "the production output must start");
        engine.set_playing(true);
    }
    void recompile(const Song& song) {
        assets = load_clip_assets(song, cache, rate, &report);
        std::string error;
        require(engine.recompile(song, 0, &error, assets), "recompile: " + error);
    }
    Stereo pump(std::size_t frames) {
        std::vector<float> stereo(frames * 2, -9.0F);
        require(output.pump(stereo), "the production callback must render");
        return {{stereo.begin(), stereo.begin() + static_cast<std::ptrdiff_t>(frames)},
                {stereo.begin() + static_cast<std::ptrdiff_t>(frames), stereo.end()}};
    }
};

const float centre = strip_gain(MixerStrip{}, false).left;

bool near(float a, float b, float tolerance = 1e-6F) { return std::abs(a - b) <= tolerance; }

// features/audio_clips.feature:
//   Scenario: A clip starts on the exact sample of its tick
void start_sample() {
    const auto dir = work_dir("start_sample");
    auto song = clip_song();
    song.audio_files.push_back(write_ramp(dir / "ramp.wav"));
    add_clip(song, 0, 700, 0, ramp_frames);   // 700 ticks at 120 BPM, 480 a beat: 35000
    Player player;
    player.prepare(song);
    require(!player.report.missing[0], "the ramp file loads");
    const auto start = sample_for_tick(player.engine.published_clock(), 700.0);
    require(start == 35000, "tick 700 falls on sample 35000");
    const auto heard = player.pump(start + ramp_frames + 1000);
    require(probe::first_nonzero(heard.left) == start, "the clip starts on its sample");
    for (std::uint64_t frame = 0; frame < ramp_frames; ++frame) {
        require(near(heard.left[start + frame], ramp_left(frame) * centre) &&
                    near(heard.right[start + frame], ramp_right(frame) * centre),
                "frame " + std::to_string(frame) + " of the file plays at its sample");
    }
    for (std::uint64_t sample = start + ramp_frames; sample < heard.left.size(); ++sample)
        require(heard.left[sample] == 0.0F, "nothing plays after the clip ends");
}

// features/audio_clips.feature:
//   Scenario: A trimmed clip plays only its stretch of the file
void offset_length() {
    const auto dir = work_dir("offset_length");
    auto song = clip_song();
    song.audio_files.push_back(write_ramp(dir / "ramp.wav"));
    add_clip(song, 0, 480, 1000, 5000);   // one beat in: sample 24000
    Player player;
    player.prepare(song);
    const auto heard = player.pump(40000);
    require(probe::first_nonzero(heard.left) == 24000, "the trimmed clip starts on its tick");
    for (std::uint64_t frame = 0; frame < 5000; ++frame)
        require(near(heard.left[24000 + frame], ramp_left(1000 + frame) * centre),
                "the clip plays from frame 1000 of the file");
    require(heard.left[29000] == 0.0F && probe::peak({heard.left.data() + 29000, 11000}) == 0.0F,
            "the clip stops after 5000 frames");
}

// features/audio_clips.feature:
//   Scenario: A clip's gain scales what it plays
void gain() {
    const auto dir = work_dir("gain");
    auto song = clip_song();
    song.audio_files.push_back(write_ramp(dir / "ramp.wav"));
    add_clip(song, 0, 0, 0, ramp_frames).gain_db = -6.02;
    Player player;
    player.prepare(song);
    const auto heard = player.pump(ramp_frames);
    const double expected = std::pow(10.0, -6.02 / 20.0);
    for (std::uint64_t frame = 100; frame < ramp_frames; frame += 997) {
        const double ratio = heard.left[frame] / (ramp_left(frame) * centre);
        require(std::abs(ratio - expected) < 1e-5, "-6.02 dB halves the clip: " +
                                                       std::to_string(ratio));
    }
}

// features/audio_clips.feature:
//   Scenario: Fades ramp a clip in from silence and out to silence
void fades() {
    const auto dir = work_dir("fades");
    auto song = clip_song();
    // A constant file, so the envelope is what is heard.
    write_stereo(dir / "dc.wav", ramp_frames, [](std::uint64_t) { return 0.5F; },
                 [](std::uint64_t) { return 0.5F; });
    song.audio_files.push_back({dir / "dc.wav", ramp_frames, 48000, 2});
    auto& clip = add_clip(song, 0, 0, 0, 12000);
    clip.fade_in_frames = 1000;
    clip.fade_out_frames = 2000;
    Player player;
    player.prepare(song);
    const auto heard = player.pump(12000);
    const auto envelope = [&](std::size_t frame) { return heard.left[frame] / (0.5F * centre); };
    require(envelope(0) == 0.0F, "the fade-in starts from silence");
    require(near(envelope(500), 0.5F, 1e-4F), "half way through the fade-in is half level");
    for (std::size_t frame = 1; frame < 1000; ++frame)
        require(envelope(frame) > envelope(frame - 1), "the fade-in rises on every frame");
    require(near(envelope(1000), 1.0F, 1e-6F) && near(envelope(9999), 1.0F, 1e-6F),
            "between the fades the clip is at full level");
    for (std::size_t frame = 10001; frame < 12000; ++frame)
        require(envelope(frame) < envelope(frame - 1), "the fade-out falls on every frame");
    require(near(envelope(10999), 0.5F, 1e-3F), "half way through the fade-out is half level");
    require(envelope(11999) == 0.0F, "the fade-out ends in silence");
}

// features/audio_clips.feature:
//   Scenario: Mute, solo, pan and the fader act on a clip's track while it plays
void mixer_live() {
    const auto dir = work_dir("mixer_live");
    auto song = clip_song(2);
    song.audio_files.push_back(write_ramp(dir / "ramp.wav", 48000));
    add_clip(song, 0, 0, 0, 48000, 0);
    add_clip(song, 0, 0, 0, 48000, 1);
    song.tracks[1].mix.mute = true;
    Player player;
    player.prepare(song);
    const auto* engine_before = &player.engine;
    auto frame_of = [](std::uint64_t position) { return position; };

    auto heard = player.pump(block);
    require(near(heard.left[100], ramp_left(frame_of(100)) * centre),
            "one audible track of the two plays");

    // Mute track 0 as well: the bus is silent.
    song.tracks[0].mix.mute = true;
    player.engine.apply_mix(song);
    heard = player.pump(block);
    require(probe::peak(heard.left) == 0.0F && probe::peak(heard.right) == 0.0F,
            "muting the clip's track silences it");

    // Unmute both and solo track 1: only track 1 is heard, once.
    song.tracks[0].mix.mute = false;
    song.tracks[1].mix.mute = false;
    song.tracks[1].mix.solo = true;
    player.engine.apply_mix(song);
    heard = player.pump(block);
    const std::uint64_t base = 2 * block;
    require(near(heard.left[10], ramp_left(base + 10) * centre),
            "soloing one track leaves exactly one clip on the bus");

    // Hard left on the soloed track at +6 dB: the right channel empties and
    // the left carries the clip at the fader's gain.
    song.tracks[1].mix.pan = -1.0;
    song.tracks[1].mix.gain_db = 6.0;
    player.engine.apply_mix(song);
    heard = player.pump(block);
    const float fader = static_cast<float>(db_to_linear(6.0));
    require(probe::peak(heard.right) < 1e-7F, "hard left empties the right channel");
    require(near(heard.left[10], ramp_left(3 * block + 10) * fader, 1e-5F),
            "the fader's gain reaches the bus");
    require(&player.engine == engine_before, "no rebuild");
}

// features/audio_clips.feature:
//   Scenario: Editing clips while the song plays does not interrupt it
void recompile_continuity() {
    const auto dir = work_dir("recompile_continuity");
    auto song = clip_song();
    song.audio_files.push_back(write_ramp(dir / "ramp.wav"));
    add_clip(song, 0, 0, 0, ramp_frames);
    Player reference;
    reference.prepare(song);
    const auto expected = reference.pump(16 * block);

    Player edited;
    edited.prepare(song);
    Stereo heard;
    for (int chunk = 0; chunk < 16; ++chunk) {
        // A recompile of the same song every other block: nothing changes in
        // what is heard, and the playhead never moves.
        if (chunk % 2 == 1) edited.recompile(song);
        auto part = edited.pump(block);
        heard.left.insert(heard.left.end(), part.left.begin(), part.left.end());
    }
    require(heard.left == expected.left, "recompiles are seamless, sample for sample");

    // Moving the clip later takes effect from the next block, without a
    // rewind: the playhead is at 16 blocks and the clip now starts at 20.
    constexpr Tick moved = 205;   // sample 10250, at 50 samples a tick
    song.audio_clips[0].start = moved;
    edited.recompile(song);
    const auto after = edited.pump(8 * block);
    require(probe::first_nonzero(after.left) == static_cast<std::size_t>(moved * 50) - 16 * block,
            "the moved clip starts where it was moved to, the playhead kept its place");
}

// features/audio_clips.feature:
//   Scenario: A clip on an instrument's track sums with the instrument
void clap_sum() {
    const auto dir = work_dir("clap_sum");
    const auto make_song = [&](bool notes, bool clip) {
        auto song = clip_song();
        song.clips = {{0, 0, 0, 1}};
        song.tracks[0].instrument = {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", {}};
        if (notes) {
            Trigger note;
            note.start = 0;
            note.duration = 240;
            note.musical_data = Note{60, 1.0F, 0.0F};
            (void)song.patterns[0].pattern.add(note);
        }
        song.audio_files.push_back({dir / "ramp.wav", ramp_frames, 48000, 2});
        if (clip) add_clip(song, 0, 60, 0, ramp_frames);
        return song;
    };
    write_ramp(dir / "ramp.wav");
    const auto render = [&](const Song& song) {
        Player player;
        std::string error;
        player.engine.set_instrument(
            0, ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error));
        require(player.engine.has_instrument(0), "the CLAP fixture loads: " + error);
        player.prepare(song);
        return player.pump(30000);
    };
    const auto both = render(make_song(true, true));
    const auto instrument = render(make_song(true, false));
    const auto clip = render(make_song(false, true));
    require(probe::peak(instrument.left) > 0.1F && probe::peak(clip.left) > 0.1F,
            "the instrument and the clip each sound alone");
    for (std::size_t sample = 0; sample < both.left.size(); ++sample)
        require(std::abs(both.left[sample] - (instrument.left[sample] + clip.left[sample])) <=
                        1e-6F &&
                    std::abs(both.right[sample] - (instrument.right[sample] + clip.right[sample])) <=
                        1e-6F,
                "sample " + std::to_string(sample) + " is the instrument plus the clip");
}

// features/audio_clips.feature:
//   Scenario: Overlapping clips on one track both sound
void overlap_sum() {
    const auto dir = work_dir("overlap_sum");
    auto song = clip_song();
    song.audio_files.push_back(write_ramp(dir / "ramp.wav"));
    add_clip(song, 0, 0, 0, 10000);
    add_clip(song, 0, 100, 2000, 10000);   // sample 5000, frame 2000 on
    Player player;
    player.prepare(song);
    const auto heard = player.pump(20000);
    for (std::uint64_t sample = 0; sample < 15000; sample += 101) {
        float expected = 0.0F;
        if (sample < 10000) expected += ramp_left(sample);
        if (sample >= 5000) expected += ramp_left(2000 + sample - 5000);
        require(near(heard.left[sample], expected * centre, 2e-6F),
                "overlapping clips sum at sample " + std::to_string(sample));
    }
}

// features/audio_clips.feature:
//   Scenario: A bounce is the clips the engine plays, sample for sample
void bounce_readback() {
    const auto dir = work_dir("bounce_readback");
    auto song = clip_song(2);
    song.audio_files.push_back(write_ramp(dir / "ramp.wav"));
    song.audio_files.push_back({fs::path(BLOKKILY_AUDIO_FIXTURES) / "sine1k_44k1_pcm16.wav",
                                44100, 44100, 1});
    add_clip(song, 0, 240, 0, ramp_frames).fade_out_frames = 3000;
    auto& tone = add_clip(song, 1, 1000, 4410, 30000, 1);
    tone.gain_db = -3.0;
    tone.fade_in_frames = 441;
    song.tracks[1].mix.pan = 0.5;

    Player live;
    live.prepare(song);
    require(live.report.missing_count() == 0, "both files load");
    const auto samples = live.engine.song_samples();
    const auto heard = live.pump(samples);

    Player exporter;
    exporter.prepare(song);
    exporter.engine.set_playing(false);
    std::string error;
    const auto file = dir / "bounce.wav";
    const auto report = bounce_song(exporter.engine, file, WaveFormat::float32, 0, &error);
    require(report.has_value(), "bounce: " + error);
    const auto read = read_wave(file, &error);
    require(read.has_value() && read->channels == 2 && read->frames == samples,
            "the bounce reads back with the song's length: " + error);
    for (std::uint64_t frame = 0; frame < samples; ++frame)
        require(read->interleaved[frame * 2] == heard.left[frame] &&
                    read->interleaved[frame * 2 + 1] == heard.right[frame],
                "bounce frame " + std::to_string(frame) + " is what the engine played");
    require(probe::peak(heard.left) > 0.1F, "the bounce is not silence");
}

// features/audio_clips.feature:
//   Scenario: A clip after a tempo change starts where the tempo map puts it
void tempo_map() {
    const auto dir = work_dir("tempo_map");
    auto song = clip_song();
    song.tempo.set({1920, 60.0, false});   // bar 2 at 60 BPM
    song.audio_files.push_back(write_ramp(dir / "ramp.wav"));
    add_clip(song, 0, 2400, 0, ramp_frames);
    Player player;
    player.prepare(song);
    const TickClock clock(song.tempo, song.ticks_per_beat(), rate);
    const double expected = clock.sample_at(2400);
    require(std::abs(expected - 144000.0) < 1e-6, "tick 2400 is at 144000 under the map");
    require(sample_for_tick(player.engine.published_clock(), 2400.0) == 144000,
            "the engine's clock agrees");
    const auto heard = player.pump(170000);
    require(probe::first_nonzero(heard.left) == 144000,
            "the clip starts at clock.sample_at(start)");
    require(near(heard.left[144000 + 777], ramp_left(777) * centre),
            "and plays its frames at the engine rate from there");
    // The song lasts until the clip's last frame: its end is in seconds.
    require(player.engine.song_samples() >= 144000 + ramp_frames,
            "the song is long enough to hold the whole clip");
}

// features/audio_clips.feature:
//   Scenario: A file at another sample rate plays at its own pitch and length
void resampled_file() {
    auto song = clip_song();
    song.audio_files.push_back({fs::path(BLOKKILY_AUDIO_FIXTURES) / "sine1k_44k1_pcm16.wav",
                                44100, 44100, 1});
    add_clip(song, 0, 480, 0, 44100);
    Player player;
    player.prepare(song);
    require(!player.report.missing[0], "the 44.1 kHz fixture loads");
    const auto heard = player.pump(24000 + 48000 + 2000);
    const auto first = probe::first_nonzero(heard.left, 1e-4F);
    require(first.has_value() && *first >= 24000 && *first <= 24002,
            "the clip starts on its beat");
    const std::span<const float> tone{heard.left.data() + 24000, 48000};
    const double frequency = probe::dominant_frequency(tone, rate, 990.0, 1010.0, 0.5);
    require(std::abs(frequency - 1000.0) <= 1.0, "a 44.1 kHz file keeps its pitch: " +
                                                      std::to_string(frequency));
    require(probe::peak({heard.left.data() + 24000 + 48000 + 10, 1000}) == 0.0F,
            "one second of file is one second of song (48000 frames)");
    // Mono files sound on both sides.
    require(near(heard.left[30000], heard.right[30000]), "a mono file plays in both channels");
}

// features/audio_clips.feature:
//   Scenario: A project folder moved whole still finds its audio
void project_paths() {
    const auto root = work_dir("project_paths");
    const auto before = root / "before";
    auto song = clip_song();
    song.audio_files.push_back(write_ramp(before / "audio" / "ramp.wav"));
    add_clip(song, 0, 480, 0, ramp_frames);
    Project project;
    project.song = song;
    std::string error;
    require(ProjectFile::save(project, before / "song.blok", &error), "save: " + error);
    std::ifstream saved(before / "song.blok");
    std::stringstream text;
    text << saved.rdbuf();
    require(text.str().find("audiofile audio/ramp.wav ") != std::string::npos,
            "a file inside the project folder is stored relative to it");

    const auto after = root / "after";
    fs::rename(before, after);
    const auto loaded = ProjectFile::load(after / "song.blok", &error);
    require(loaded.has_value(), "load: " + error);
    require(loaded->song.audio_files.at(0).path == after / "audio" / "ramp.wav",
            "the file resolves inside the moved folder");
    Player player;
    player.prepare(loaded->song);
    require(!player.report.missing[0], "the moved file loads");
    const auto heard = player.pump(30000);
    require(probe::first_nonzero(heard.left) == 24000 &&
                near(heard.left[24005], ramp_left(5) * centre),
            "the moved project plays its clip");
}

// features/audio_clips.feature:
//   Scenario: A missing or changed file is flagged and stays silent
void missing_and_changed() {
    const auto dir = work_dir("missing_and_changed");
    auto song = clip_song(2);
    song.audio_files.push_back(write_ramp(dir / "gone.wav"));
    song.audio_files.push_back(write_ramp(dir / "changed.wav"));
    song.audio_files.push_back(write_ramp(dir / "kept.wav"));
    add_clip(song, 0, 0, 0, ramp_frames, 0);
    add_clip(song, 1, 0, 0, ramp_frames, 0);
    add_clip(song, 2, 0, 0, ramp_frames, 1);
    song.tracks[1].mix.pan = 1.0;   // the kept file, alone on the right
    song.tracks[0].mix.pan = -1.0;  // the other two, on the left

    fs::remove(dir / "gone.wav");
    write_ramp(dir / "changed.wav", ramp_frames / 2);   // replaced behind the project's back

    Player player;
    player.prepare(song);
    require(player.report.missing == std::vector<bool>{true, true, false},
            "the removed and the changed file are missing, the other is not");
    require(player.report.reasons[1].find("expects") != std::string::npos,
            "a changed file says what the project expected: " + player.report.reasons[1]);
    const auto heard = player.pump(ramp_frames);
    // Hard right leaves cos(pi/2) of the kept track on the left: 1e-17, not 0.
    require(probe::peak(heard.left) < 1e-9F, "missing files are never played");
    require(near(heard.right[123], ramp_right(123)), "the song still plays what it has");

    // Put the file back as it was: the next recompile finds it and plays it.
    write_ramp(dir / "changed.wav");
    player.recompile(song);
    require(player.report.missing == std::vector<bool>{true, false, false},
            "a restored file is found again");
    player.engine.seek(0);
    const auto restored = player.pump(block);
    require(near(restored.left[10], ramp_left(10)), "and plays again");
}

// features/audio_clips.feature:
//   Scenario: An import decoded off the control thread is not decoded again
void adopted_import() {
    const auto dir = work_dir("adopted_import");
    const auto ref = write_ramp(dir / "ramp.wav");
    const AudioFileInfo info{ref.frames, ref.sample_rate, ref.channels};
    // The import worker's own store decodes the file...
    AudioAssetCache worker;
    const auto decoded = worker.load(ref.path, rate, info);
    require(decoded != nullptr && worker.decode_count() == 1, "the worker decodes the file");
    // ...and the application's store takes the result over.
    AudioAssetCache shared;
    shared.adopt(ref.path, info, decoded);
    auto song = clip_song();
    song.audio_files.push_back(ref);
    add_clip(song, 0, 0, 0, ramp_frames);
    ClipAssetReport report;
    const auto assets = load_clip_assets(song, shared, rate, &report);
    require(assets.at(0) == decoded && shared.decode_count() == 0,
            "the song's clips play the adopted decode without reading the file again");
    // A file changed on disk afterwards is noticed, as for any load.
    write_ramp(dir / "ramp.wav", ramp_frames / 2);
    const auto reloaded = load_clip_assets(song, shared, rate, &report);
    require(!reloaded.at(0) && report.missing.at(0),
            "the changed file is missing, not the adopted audio played wrong");
}

} // namespace

int main(int argc, char** argv) {
    const std::map<std::string, void (*)()> cases{
        {"start_sample", start_sample},
        {"offset_length", offset_length},
        {"gain", gain},
        {"fades", fades},
        {"mixer_live", mixer_live},
        {"recompile_continuity", recompile_continuity},
        {"clap_sum", clap_sum},
        {"overlap_sum", overlap_sum},
        {"bounce_readback", bounce_readback},
        {"tempo_map", tempo_map},
        {"resampled_file", resampled_file},
        {"project_paths", project_paths},
        {"missing_and_changed", missing_and_changed},
        {"adopted_import", adopted_import},
    };
    if (argc != 2 || !cases.contains(argv[1])) {
        std::cerr << "usage: blokkily_audio_clips_tests <case>\n";
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
