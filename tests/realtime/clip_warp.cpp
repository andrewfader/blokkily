// features/clip_warp.feature: Renditions render off the audio thread and swap
// in without the render callback allocating.
//
// Two warped clips (an octave up, and stretched by 1.5) are prepared before
// their renditions exist, so the callback first plays them as silence. The
// production WarpRenderer renders them on its own worker thread while the
// callback keeps running; a second control thread then recompiles the
// arrangement with the renditions and without them, back and forth, and
// finally with them. Not one process() call may allocate or free, the
// renditions (and the files they came from) are let go on the recompiling
// thread, and the audio proves the octave-up rendition really played.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/audio_asset.hpp"
#include "blokkily/audio/audio_clips.hpp"
#include "blokkily/audio/clip_warp.hpp"
#include "blokkily/audio/song_engine.hpp"

#include <atomic>
#include <cmath>
#include <numbers>
#include <string>
#include <thread>
#include <vector>

namespace {

using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

constexpr std::size_t block = 256;
constexpr std::size_t before_render = 50;       // calls before the renderer starts
constexpr std::size_t after_swap = 800;         // calls once the renditions are in
constexpr std::size_t call_limit = 2'000'000;   // a bound, never reached when it works
constexpr std::uint64_t tone_frames = 96000;

} // namespace

BLOKKILY_REALTIME_CASE(clip_warp) {
    using namespace blokkily;

    // A 1 kHz tone held in memory under a path of its own, as a recorded take
    // is: nothing here reads a file.
    const std::filesystem::path tone_path = "/nonexistent/realtime-clip-warp/tone1k.wav";
    AudioAssetCache cache;
    {
        AudioAsset tone;
        tone.rate = 48000;
        tone.left.resize(tone_frames);
        for (std::uint64_t frame = 0; frame < tone_frames; ++frame)
            tone.left[frame] = static_cast<float>(
                0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 * static_cast<double>(frame) /
                               48000.0));
        cache.insert(tone_path, std::move(tone));
    }
    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks = {Track{}, Track{}};
    song.clips = {};
    song.audio_files = {{tone_path, tone_frames, 48000, 1}};
    AudioClip octave;
    octave.id = 1;
    octave.length_frames = tone_frames;
    octave.warp.semitones = 12;
    AudioClip stretched = octave;
    stretched.id = 2;
    stretched.track = 1;
    stretched.warp = ClipWarp{};
    stretched.warp.ratio = 1.5;
    song.audio_clips = {octave, stretched};
    song.tracks[1].mix.mute = true;   // the octave alone is measured

    ClipAssetReport report;
    const auto assets = load_clip_assets(song, cache, 48000.0, &report);
    require(report.missing_count() == 0, "the tone is in the cache");

    SongEngine engine;
    std::string error;
    require(engine.prepare(song, 48000.0, block, 0, &error, assets), "prepare: " + error);
    engine.set_playing(true);

    std::vector<float> left(block), right(block);
    test::AllocationCount total;
    float before = 0.0F;
    const auto process = [&] {
        test::AllocationGuard guard;
        engine.process({std::span<float>{left}, std::span<float>{right}});
        total += guard.count();
    };
    for (std::size_t call = 0; call < before_render; ++call) {
        process();
        before = std::max(before, probe::peak(left));
    }
    require(before == 0.0F, "warped clips are silent while their renditions render");

    // The renditions render on the renderer's worker while the callback runs.
    WarpRenderer renderer;
    const TickClock clock(song.tempo, song.ticks_per_beat(), 48000.0);
    for (const auto& clip : song.audio_clips) {
        auto plan = plan_clip_warp(song, clip, clock, assets[0]->frames);
        require(plan.has_value(), "each warped clip has a plan");
        renderer.submit(assets[0], std::move(*plan));
    }

    std::atomic<bool> swapped{false};
    std::atomic<int> recompiles{0};
    std::thread control([&] {
        renderer.wait_idle();
        ClipRenditions renditions;
        for (auto& done : renderer.take_finished()) {
            if (!done.asset) continue;
            cache.insert_derived(done.key,
                                 std::make_shared<const AudioAsset>(std::move(*done.asset)));
            renditions[done.key] = cache.derived(done.key);
        }
        // In and out and in again, the way edits and renders interleave; the
        // last arrangement queued has the renditions.
        for (int pass = 0; pass < 40; ++pass) {
            const auto held = pass % 2 == 0 ? renditions : ClipRenditions{};
            while (!engine.recompile(song, 0, nullptr, assets, held)) std::this_thread::yield();
            ++recompiles;
        }
        while (!engine.recompile(song, 0, nullptr, assets, renditions)) std::this_thread::yield();
        ++recompiles;
        swapped.store(true, std::memory_order_release);
    });

    std::size_t calls = before_render;
    while (!swapped.load(std::memory_order_acquire) && calls < call_limit) {
        process();
        ++calls;
    }
    control.join();
    require(swapped.load(), "the renditions were rendered and handed to the engine");
    std::vector<float> heard;
    heard.reserve(after_swap * block);
    for (std::size_t call = 0; call < after_swap; ++call) {
        process();
        heard.insert(heard.end(), left.begin(), left.end());
    }
    require_no_allocations(total, "SongEngine::process while warp renditions render and swap in");
    require(recompiles.load() == 41, "every recompile reached the engine");
    require(renderer.rendered() == 2, "both renditions were rendered by the worker");

    // The octave-up rendition was heard once it swapped in.
    require(probe::peak(heard) > 0.2F, "the rendition plays after it swaps in");
    int sounding = 0;
    for (std::size_t from = 0; from + 8192 <= heard.size(); from += 8192) {
        const std::span<const float> window{heard.data() + from, 8192};
        if (probe::rms(window) < 0.2) continue;   // the part of the loop after the clip
        ++sounding;
        const double hz = probe::dominant_frequency(window, 48000.0, 500.0, 3000.0, 5.0);
        require(std::abs(hz - 2000.0) <= 10.0, "what plays is the octave up: " + std::to_string(hz));
    }
    require(sounding >= 4, "the octave-up rendition sounds for most of each pass");
}
