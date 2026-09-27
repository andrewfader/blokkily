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
#include <vector>

namespace blokkily {
class DiskStream;
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
    DiskStream* stream = nullptr;
};

// One clip as the render callback plays it. Every count is in frames at the
// engine rate.
struct ClipRegion {
    std::uint64_t start = 0;      // song sample of the region's first frame
    std::uint64_t frames = 0;     // how many frames the region plays
    const float* left = nullptr;  // the region's first frame
    const float* right = nullptr; // the same as `left` for a mono file
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
void compile_clip_regions(ArrangementClips& target, const Song& song, const TickClock& clock,
                          const AudioAssets& assets, const ClipRenditions& renditions = {});

// Adds whatever clip regions of track `track` sound in
// [song_position, song_position + frames) to `buffer`. Overlapping regions
// sum (decision 6). Runs only while the transport plays from the timeline.
void sum_clip_regions(ClipPlayback& playback, const ArrangementClips& clips, std::size_t track,
                      StereoBlock buffer, std::uint64_t song_position,
                      bool from_timeline) noexcept;

} // namespace blokkily::engine
