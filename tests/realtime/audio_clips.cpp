// features/audio_clips.feature: Audio clips play without the render callback
// allocating.
//
// Two tracks of audio clips (a 48 kHz file, and a 44.1 kHz file resampled to
// the engine rate, overlapping, with gain and fades) play through the
// production SongEngine for a thousand process() calls, across a seek and
// several loop wraps, while a second thread recompiles the arrangement with
// the clips moved back and forth, the way a drag does. Not one process() call
// may allocate or free (the clips' audio is let go on the recompiling thread,
// never the callback's), and the audio proves the clips really played.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/audio_asset.hpp"
#include "blokkily/audio/audio_clips.hpp"
#include "blokkily/audio/song_engine.hpp"

#include <atomic>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;
constexpr std::size_t seek_before_call = 400;
constexpr std::uint64_t seek_target = 30000;
const std::filesystem::path fixtures = BLOKKILY_AUDIO_FIXTURES;

blokkily::Song clip_song(blokkily::Tick shift) {
    using namespace blokkily;
    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks = {Track{}, Track{}};
    song.tracks[0].mix.pan = -1.0;   // the hits, alone on the left
    song.tracks[1].mix.pan = 1.0;    // the tone, alone on the right
    song.clips = {};
    song.audio_files = {{fixtures / "hits8_48k_pcm16.wav", 48000, 48000, 1},
                        {fixtures / "sine1k_44k1_pcm16.wav", 44100, 44100, 1}};
    AudioClip hits;
    hits.id = 1;
    hits.file = 0;
    hits.start = shift;
    hits.length_frames = 48000;
    hits.gain_db = -3.0;
    AudioClip overlap = hits;
    overlap.id = 2;
    overlap.start = 480 + shift;   // overlaps the first by half a second
    overlap.offset_frames = 6000;
    overlap.length_frames = 30000;
    overlap.fade_in_frames = 500;
    overlap.fade_out_frames = 700;
    AudioClip tone;
    tone.id = 3;
    tone.track = 1;
    tone.file = 1;
    tone.start = 240;
    tone.offset_frames = 100;
    tone.length_frames = 40000;
    tone.fade_in_frames = 441;
    song.audio_clips = {hits, overlap, tone};
    return song;
}

} // namespace

BLOKKILY_REALTIME_CASE(audio_clips) {
    using namespace blokkily;

    AudioAssetCache cache;
    ClipAssetReport report;
    const auto still = clip_song(0);
    const auto moved = clip_song(120);
    const auto assets = load_clip_assets(still, cache, 48000.0, &report);
    require(report.missing_count() == 0, "both fixtures must load");

    SongEngine engine;
    std::string error;
    require(engine.prepare(still, 48000.0, block, 0, &error, assets), "prepare: " + error);
    const auto song_samples = engine.song_samples();
    require(calls * block > 2 * song_samples, "the run wraps the song at least twice");

    // The drag: another thread recompiling the moved and the unmoved song as
    // fast as the engine takes them. It holds its own copies of the asset
    // pointers, so whichever arrangement drops the last reference to one does
    // so here.
    std::atomic<bool> running{true};
    std::atomic<int> recompiles{0};
    std::thread dragging([&] {
        bool flip = false;
        while (running.load(std::memory_order_acquire)) {
            const auto held = assets;
            if (engine.recompile(flip ? moved : still, 0, nullptr, held)) ++recompiles;
            flip = !flip;
            std::this_thread::yield();
        }
    });

    std::vector<float> left(calls * block, -1.0F);
    std::vector<float> right(calls * block, -1.0F);
    engine.set_playing(true);
    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        if (call == seek_before_call) engine.seek(seek_target);
        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        {
            test::AllocationGuard guard;
            engine.process({out_left, out_right});
            total += guard.count();
        }
    }
    running.store(false, std::memory_order_release);
    dragging.join();
    require_no_allocations(total, "1000 SongEngine::process calls playing audio clips");
    require(recompiles.load() > 0, "the second thread's recompiles reached the engine");

    // The hits (64-frame bursts every 6000 frames) were heard, pass after pass.
    require(probe::rising_edges(left, 0.05F) >= 3 * 8, "the hits clips played on every pass");
    // The tone, alone on the right, was heard there.
    require(probe::peak(right) > 0.2F, "the resampled tone clip played");
}
