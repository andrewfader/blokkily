// features/track_freeze.feature: a frozen track sounds as it did, through
// the production engine, without its instrument or inserts loaded.
#include "engine_graph.hpp"

#include "blokkily/audio/audio_asset.hpp"
#include "blokkily/audio/audio_clips.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/take_writer.hpp"
#include "blokkily/project/project.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace blokkily;
namespace fs = std::filesystem;
namespace {

void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

constexpr double rate = 48000.0;

Song make_song() {
    Song song;
    song.patterns = {{"Freeze", Pattern(1920, 480)}};
    for (int step = 0; step < 4; ++step) {
        Trigger trigger;
        trigger.start = step * 480;
        trigger.duration = 240;
        trigger.musical_data = Note{static_cast<std::int16_t>(60 + step), 0.8F};
        trigger.locks = {{"tone", 1, 1.0, ParameterLock::Kind::automation}};
        (void)song.patterns[0].pattern.add(trigger);
    }
    song.tracks.assign(2, Track{});
    song.tracks[0].instrument = {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", {}};
    song.tracks[1].instrument = song.tracks[0].instrument;
    // A latent effect on the frozen track, so its compensation is part of it.
    song.tracks[0].inserts.push_back({{"CLAP", BLOKKILY_TEST_CLAP_EFFECT_PATH, "dev.blokkily.test.effect", {}}, false, {}});
    song.tracks[0].mix.gain_db = -3.0;
    song.tracks[0].mix.pan = 0.4;
    song.tracks[1].mix.gain_db = -9.0;
    song.clips = {{0, 0, 0, 2}, {1, 0, 0, 2}};
    return song;
}

struct Built {
    SongEngine engine;
    AudioAssetCache cache;
};

void build(Built& built, const Song& song) {
    auto graph = populate_graph(built.engine, song, {}, {});
    require(graph.error.empty(), graph.error);
    ClipAssetReport report;
    const auto assets = load_clip_assets(song, built.cache, rate, &report);
    require(report.missing_count() == 0, "every clip's file loads");
    std::string error;
    require(built.engine.prepare(song, rate, 512, 0, &error, assets), error);
    load_fresh_state(built.engine, song, graph);
}

WaveData bounce(SongEngine& engine, const fs::path& file, std::optional<OutputTap> tap = {}) {
    std::string error;
    BounceOptions options;
    options.source = tap;
    require(bounce_song(engine, file, WaveFormat::float32, 24000, options, &error).has_value(), error);
    auto wave = read_wave(file, &error);
    require(wave.has_value(), error);
    return *wave;
}

// The song frozen as AppController::freezeTrack freezes it: track 0's
// pre-fader render as its freeze clip.
Song frozen_from(const Song& song, const fs::path& dir) {
    Built built;
    build(built, song);
    const auto file = dir / "freeze-1.wav";
    const auto render = bounce(built.engine, file, OutputTap{BusKind::track, 0, true});
    auto frozen = song;
    RecordedTake take{0, 0, render.frames, 2, static_cast<std::uint32_t>(rate), file, nullptr, {}};
    const auto id = commit_take(frozen, take, built.engine.published_clock(), 0);
    require(id.has_value(), "the freeze clip is placed");
    for (auto& clip : frozen.audio_clips)
        if (clip.id == *id) clip.freeze = true;
    frozen.tracks[0].frozen = true;
    return frozen;
}

void sounds_the_same() {
    const auto dir = fs::path(BLOKKILY_FREEZE_WORK) / "same";
    fs::create_directories(dir);
    const auto song = make_song();
    Built live;
    build(live, song);
    require(live.engine.output_latency() == 64, "the effect's latency is compensated before the freeze");
    const auto before = bounce(live.engine, dir / "before.wav");
    const auto frozen = frozen_from(song, dir);
    Built cold;
    build(cold, frozen);
    require(cold.engine.processor(track_instrument(0)) == nullptr &&
                cold.engine.processor({BusKind::track, 0, 0}) == nullptr,
            "a frozen track's instrument and insert are not loaded");
    require(cold.engine.processor(track_instrument(1)) != nullptr, "the other track's is");
    const auto after = bounce(cold.engine, dir / "after.wav");
    // The render runs to the end of the track's tail, so the frozen song
    // ends there: what it adds past the live song's end is silence.
    require(after.frames >= before.frames, "the frozen song is at least as long");
    double worst = 0.0, peak = 0.0, past = 0.0;
    for (std::size_t i = 0; i < after.interleaved.size(); ++i) {
        if (i >= before.interleaved.size()) {
            past = std::max(past, static_cast<double>(std::abs(after.interleaved[i])));
            continue;
        }
        worst = std::max(worst, static_cast<double>(std::abs(before.interleaved[i] - after.interleaved[i])));
        peak = std::max(peak, static_cast<double>(std::abs(before.interleaved[i])));
    }
    require(past < 1e-6, "past the live song's end the frozen one is silent");
    std::cout << "frozen against live: worst difference " << worst << " of peak " << peak << '\n';
    require(peak > 0.05, "the song sounds");
    require(worst < 1e-6, "frozen, the song is the same sample for sample");

    // The fader still works on the frozen track, after the freeze.
    auto quieter = frozen;
    quieter.tracks[0].mix.gain_db = -9.0;
    auto louder_live = song;
    louder_live.tracks[0].mix.gain_db = -9.0;
    Built a, b;
    build(a, quieter);
    build(b, louder_live);
    const auto x = bounce(a.engine, dir / "fader-frozen.wav");
    const auto y = bounce(b.engine, dir / "fader-live.wav");
    double moved = 0.0;
    for (std::size_t i = 0; i < std::min(x.interleaved.size(), y.interleaved.size()); ++i)
        moved = std::max(moved, static_cast<double>(std::abs(x.interleaved[i] - y.interleaved[i])));
    require(moved < 1e-6, "a fader moved after the freeze sounds as it would have unfrozen");
}

void other_clips() {
    // A track's own audio clip is in its freeze, so it is not heard twice.
    const auto dir = fs::path(BLOKKILY_FREEZE_WORK) / "clips";
    fs::create_directories(dir);
    auto song = make_song();
    {
        std::vector<float> samples(12000, 0.25F);
        WaveWriter writer;
        std::string error;
        require(writer.open(dir / "own.wav", 48000, WaveFormat::float32, &error) &&
                    writer.write(samples, samples, &error) && writer.close(&error), error);
    }
    song.audio_files.push_back({dir / "own.wav", 12000, 48000, 2});
    AudioClip own;
    own.id = song.next_audio_clip_id();
    own.track = 0;
    own.file = 0;
    own.start = 960;
    own.length_frames = 12000;
    song.audio_clips.push_back(own);
    Built live;
    build(live, song);
    const auto before = bounce(live.engine, dir / "before.wav");
    Built cold;
    build(cold, frozen_from(song, dir));
    const auto after = bounce(cold.engine, dir / "after.wav");
    double worst = 0.0;
    for (std::size_t i = 0; i < std::min(before.interleaved.size(), after.interleaved.size()); ++i)
        worst = std::max(worst, static_cast<double>(std::abs(before.interleaved[i] - after.interleaved[i])));
    require(worst < 1e-6, "the track's own clip plays once, inside its freeze");
}

void records() {
    const auto dir = fs::path(BLOKKILY_FREEZE_WORK) / "records";
    fs::create_directories(dir);
    Project project;
    project.song = frozen_from(make_song(), dir);
    const auto text = ProjectFile::serialize(project, dir);
    require(text.find("frozen 0") != std::string::npos && text.find("freeze_clip ") != std::string::npos,
            "the freeze is saved");
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error, dir);
    require(loaded.has_value(), error);
    require(loaded->song.tracks[0].frozen && !loaded->song.tracks[1].frozen &&
                loaded->song.audio_clips.back().freeze, "and loads as saved");
    auto broken = text;
    broken.erase(broken.find("freeze_clip "), broken.find('\n', broken.find("freeze_clip ")) - broken.find("freeze_clip ") + 1);
    require(!ProjectFile::parse(broken, &error, dir).has_value(), "a frozen track without its render is refused");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "case required");
        const std::string which = argv[1];
        if (which == "sounds_the_same") sounds_the_same();
        else if (which == "other_clips") other_clips();
        else if (which == "records") records();
        else throw std::runtime_error("unknown case");
        std::cout << "PASS " << which << '\n';
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
