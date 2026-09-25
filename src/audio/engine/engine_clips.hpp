#pragma once

// Chunk stage 5 (plan F-D): audio-clip regions summed into a track's buffer.
// Item 2.2 fills these structs and the stage; until then they are empty and
// the stage adds nothing, except through the test source below.

#include "blokkily/plugins/plugin.hpp"

#include <cstdint>

namespace blokkily::engine {

// A test-only stand-in for audio clips, so that a track with no instrument
// can be proved to render through its strip before clips exist. Production
// code never sets it. It runs on the audio thread and must obey the same
// rules: no allocation, no locks, no I/O.
using TestSourceFunction = void (*)(void* context, StereoBlock track,
                                    std::uint64_t song_position) noexcept;

// Per track, owned by the engine, touched by the render callback.
struct ClipPlayback {
    TestSourceFunction test_source = nullptr;
    void* test_context = nullptr;
};

// Per arrangement: what the compiled song places on each track's clip lane.
struct ArrangementClips {};

// Adds whatever clip regions sound in [song_position, song_position + frames)
// to `track`. Runs only while the transport plays from the timeline.
void sum_clip_regions(ClipPlayback& playback, const ArrangementClips& clips, StereoBlock track,
                      std::uint64_t song_position, bool from_timeline) noexcept;

} // namespace blokkily::engine
