#include "engine_clips.hpp"

namespace blokkily::engine {

void sum_clip_regions(ClipPlayback& playback, const ArrangementClips&, StereoBlock track,
                      std::uint64_t song_position, bool from_timeline) noexcept {
    // Clips play from the arrangement, so a stopped song hears none of them.
    if (!from_timeline) return;
    if (playback.test_source != nullptr)
        playback.test_source(playback.test_context, track, song_position);
}

} // namespace blokkily::engine
