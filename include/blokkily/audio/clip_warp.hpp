#pragma once

// Clip warp (item 3.6, decision 7): stretched and pitch-shifted renditions of
// audio clips, rendered with Rubber Band off the audio thread.
//
// A warped clip never reaches the render callback as a file. The control
// thread plans what the clip must sound like at the engine's rate under the
// song's tempo map (plan_clip_warp), a worker renders that plan into memory
// (render_warp, run by WarpRenderer), and the finished rendition is kept in
// the AudioAssetCache as a derived asset under the plan's key. The engine
// then plays it exactly as it plays any decoded file: the render callback only
// ever reads decoded memory.
//
// While a clip's rendition is still being rendered the clip is SILENT. Playing
// the unstretched file instead would put its beats in the wrong places and its
// notes at the wrong pitch, which is worse than a short gap the interface
// shows as "rendering"; an export waits for every rendition first, so a
// bounce is never missing one.
//
// Rubber Band Library is GPL (see README.md): linking it makes Blokkily's
// binaries GPL as well.

#include "blokkily/audio/audio_asset.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/model/timebase.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace blokkily {

// Everything a rendition depends on, in frames at the engine rate. The clip's
// stretch of source audio (`source_offset`, `source_frames` into the source
// asset, itself decoded at `rate`) becomes `output_frames` of rendition,
// shaped by `points`: a piecewise-linear map from source frames to rendition
// frames, from (0, 0) to (source_frames, output_frames), strictly increasing in
// both. Two clips with equal plans share one rendition.
struct WarpPlan {
    std::filesystem::path file;
    std::uint32_t rate = 0;
    std::uint64_t source_offset = 0;
    std::uint64_t source_frames = 0;
    std::uint64_t output_frames = 0;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> points;
    double pitch_scale = 1.0;

    // The derived asset's key in the cache: file, map, pitch and rate.
    [[nodiscard]] std::string key() const;
    // Where source frame `frame` (relative to source_offset) lands in the
    // rendition, along the map.
    [[nodiscard]] double output_at(double frame) const noexcept;

    friend bool operator==(const WarpPlan&, const WarpPlan&) = default;
};

// What a warped clip must sound like under `clock` (the song's tempo map at the
// engine rate). `source_frames` is the length of the clip's source asset at the
// clock's rate. Nothing when the clip is not warped (ClipWarp::active() is
// false), or has no audio to play. Control thread; deterministic, so the engine
// and the application always compute the same plan for the same song.
[[nodiscard]] std::optional<WarpPlan> plan_clip_warp(const Song& song, const AudioClip& clip,
                                                     const TickClock& clock,
                                                     std::uint64_t source_frames);

// Renders `plan` from `source` (decoded at plan.rate) with Rubber Band in
// offline mode. Each stretch of constant ratio in the plan is rendered on its
// own, so a tempo step or ramp lands every beat where the plan puts it, and the
// stretches are joined by short crossfades. Slow: never on the audio thread,
// normally on WarpRenderer's worker. Returns nothing, with a reason, if the
// source does not match the plan, or when `cancel` is raised.
[[nodiscard]] std::optional<AudioAsset> render_warp(const AudioAsset& source, const WarpPlan& plan,
                                                    std::string* error = nullptr,
                                                    const std::atomic<bool>* cancel = nullptr);

// The tempo of a stretch of audio, from the periodicity of its onsets, in
// [60, 200) BPM; a tempo within 0.05 BPM of a whole number is that number.
// Nothing when no steady beat is found. Control thread.
[[nodiscard]] std::optional<double> detect_tempo(const AudioAsset& audio,
                                                 std::uint64_t offset, std::uint64_t frames);

// The renditions the engine can play, by WarpPlan::key().
using ClipRenditions = std::map<std::string, AudioAssetPtr>;

// A worker thread that renders warp plans one at a time, newest request per
// clip first served. Owned and driven by the control thread; the worker never
// touches the cache or the song. Finished renditions are collected with
// take_finished(); `on_finished` (if set) is called on the worker thread after
// each one, to wake the control thread.
class WarpRenderer {
public:
    struct Finished {
        std::string key;
        std::optional<AudioAsset> asset;   // nothing when rendering failed
        std::string error;
    };

    explicit WarpRenderer(std::function<void()> on_finished = {});
    ~WarpRenderer();
    WarpRenderer(const WarpRenderer&) = delete;
    WarpRenderer& operator=(const WarpRenderer&) = delete;

    // Asks for `plan` to be rendered from `source`. A key already queued or
    // rendering is not asked for twice. The source is held until the job is
    // done.
    void submit(AudioAssetPtr source, WarpPlan plan);
    // Drops queued requests whose key is not in `wanted` (an edit made them
    // stale) and cancels the one rendering if it is not wanted either.
    void retain(const std::set<std::string>& wanted);
    // Renditions finished since the last call, oldest first.
    [[nodiscard]] std::vector<Finished> take_finished();
    // Keys queued or rendering.
    [[nodiscard]] std::set<std::string> pending() const;
    // Blocks until nothing is queued or rendering. For an export, which must
    // not bounce a song whose clips are still silent.
    void wait_idle();
    // Renditions completed since construction.
    [[nodiscard]] std::uint64_t rendered() const noexcept {
        return rendered_.load(std::memory_order_acquire);
    }

private:
    struct Job {
        AudioAssetPtr source;
        WarpPlan plan;
        std::string key;
    };
    void run();

    std::function<void()> on_finished_;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable idle_;
    std::deque<Job> queue_;
    std::string current_;
    bool busy_ = false;
    bool stopping_ = false;
    std::atomic<bool> cancel_{false};
    std::vector<Finished> finished_;
    std::atomic<std::uint64_t> rendered_{0};
    std::thread worker_;
};

} // namespace blokkily
