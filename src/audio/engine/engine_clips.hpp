#pragma once

// Chunk stage 5 (plan F-D): audio-clip regions summed into a track's buffer
// (item 2.2).
//
// A region is one audio clip compiled for one arrangement: where in the song it
// starts, how many frames it plays, and raw pointers into decoded audio at the
// engine rate. The render callback only ever reads these pointers; the
// shared_ptrs that own the audio are held by the arrangement alongside them
// and are released on the control thread when the arrangement's slot is next
// refilled (plan F-B, "Retiring buffers").
//
// A region does not care where its frames came from. Today they are the file
// itself, resampled to the engine rate; a stretched rendition (clip warp, item
// 3.6) is the same thing rendered off the audio thread into another buffer,
// so warping a clip changes what `left`/`right`/`frames` point at and nothing
// here.

#include "blokkily/audio/audio_asset.hpp"
#include "blokkily/audio/clip_warp.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/model/timebase.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <tuple>
#include <vector>

namespace blokkily {
class DiskStream;
class DiskStreamService;
}

namespace blokkily::engine {

// A test-only stand-in source on the clip stage, so that a track with no
// instrument can be proved to render through its strip independently of
// clips. Production code never sets it. It runs on the audio thread and must
// obey the same rules: no allocation, no locks, no I/O.
using TestSourceFunction = void (*)(void* context, StereoBlock track,
                                    std::uint64_t song_position) noexcept;

// Per track, owned by the engine, touched by the render callback.
struct ClipPlayback {
    TestSourceFunction test_source = nullptr;
    void* test_context = nullptr;
    // Where a streamed region's frames are read to before the gain and fades
    // are applied. Sized by prepare().
    std::vector<float> stream_left;
    std::vector<float> stream_right;
};

// The streams the arrangements' streamed clips play (wave 4.2), one per clip,
// kept across recompiles so a clip that is edited keeps its buffered audio.
// Control thread only; the render callback reaches a stream only through
// the raw pointer in a region of the arrangement it plays, and every
// arrangement slot owns the streams its regions point at.
class StreamPool {
public:
    StreamPool();
    ~StreamPool();
    // Background threads filling the rings; 0 leaves them to service().
    // Only while nothing plays.
    void set_workers(std::size_t workers);
    [[nodiscard]] std::shared_ptr<DiskStream> acquire(std::uint64_t clip,
                                                      const StreamedFile& file,
                                                      std::uint32_t rate,
                                                      std::uint64_t first_frame);
    // Lets go of the streams no arrangement slot holds any more.
    void purge();
    // Fills every stream's ring on the calling thread; true if any took data.
    bool service();
    [[nodiscard]] std::size_t size() const noexcept { return streams_.size(); }

private:
    using Key = std::tuple<std::uint64_t, std::filesystem::path, std::uint32_t>;
    std::size_t workers_ = 1;
    std::unique_ptr<DiskStreamService> service_;
    std::map<Key, std::shared_ptr<DiskStream>> streams_;
};

// One clip as the render callback plays it. Every count is in frames at the
// engine rate.
struct ClipRegion {
    std::uint64_t start = 0;      // song sample of the region's first frame
    std::uint64_t frames = 0;     // how many frames the region plays
    const float* left = nullptr;  // the region's first frame
    const float* right = nullptr; // the same as `left` for a mono file
    // A streamed clip (wave 4.2) reads its frames from here instead, frame
    // `first` of the stream being the region's first frame.
    DiskStream* stream = nullptr;
    std::uint64_t first = 0;
    float gain = 1.0F;            // the clip's gain, linear
    std::uint64_t fade_in = 0;    // frames of linear fade from silence
    std::uint64_t fade_out = 0;   // frames of linear fade to silence
};

// Per arrangement: every track's regions, sorted by start sample.
struct ArrangementClips {
    std::vector<std::vector<ClipRegion>> tracks;
    // The owners of the audio the regions point into. Replaced, and so
    // released, only by compile_clip_regions on the control thread.
    AudioAssets held;
    // The streams the regions read, owned the same way.
    std::vector<std::shared_ptr<DiskStream>> streams;
    // How far ahead of a streamed clip its stream is cued to its first frame.
    std::uint64_t cue_window = 0;
};

// Control thread. Compiles `song`'s audio clips into `target` with `clock`
// placing each clip's start tick (the same rounding rule as the timeline's
// events). `assets` is indexed like song.audio_files; a null or absent entry
// is a missing file, whose clips are left out. An asset decoded at another
// rate than the clock's is left out too, rather than played at the wrong
// speed.
//
// A warped clip (item 3.6) plays the rendition in `renditions` whose key is
// the plan plan_clip_warp makes for it under `clock`. Until that rendition
// exists the clip is left out: silent while it renders, never played
// unstretched.
//
// A streamed asset (AudioAsset::streamed) plays through a stream from
// `pool`, one per clip; without a pool its clips are left out. A warped clip
// whose file streams still plays its rendition, which is rendered from the
// file decoded into memory: warping needs the whole clip at once.
void compile_clip_regions(ArrangementClips& target, const Song& song, const TickClock& clock,
                          const AudioAssets& assets, const ClipRenditions& renditions = {},
                          StreamPool* pool = nullptr);

// Adds whatever clip regions of track `track` sound in
// [song_position, song_position + frames) to `buffer`. Overlapping regions
// sum (decision 6). Sounds only while the transport plays from the
// timeline; stopped, it only cues the streams of the clips around the
// playhead, so pressing play finds their audio ready. `song_samples` is
// where the song wraps, for cueing the clips at its start. `blocking` reads
// streams blocking: an offline bounce only.
void sum_clip_regions(ClipPlayback& playback, const ArrangementClips& clips, std::size_t track,
                      StereoBlock buffer, std::uint64_t song_position, bool from_timeline,
                      bool blocking = false, std::uint64_t song_samples = 0) noexcept;

} // namespace blokkily::engine
