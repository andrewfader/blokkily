// The controller's part of the tempo and meter interface (item 2.1): moving the
// playhead onto a step of the open pattern, wherever the meter map and the
// pattern's own length put that step in the song.

#include "app_controller.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

void AppController::seekToPatternStep(int step) {
    if (step < 0 || song_ == nullptr || transport_ == nullptr) return;
    const auto& song = song_->song();
    const auto at = static_cast<blokkily::Tick>(std::floor(transport_->tick() + 1e-6));
    const auto track = static_cast<std::size_t>(std::max(0, song_->selectedTrack()));
    const auto open = static_cast<std::size_t>(std::max(0, song_->currentPattern()));
    // Without a clip of the pattern under the playhead, the pattern is read
    // as starting on the bar the playhead is in.
    blokkily::Tick pass = song_->barStart(song_->barAt(at));
    for (const auto& clip : song.clips) {
        if (clip.track != track || clip.pattern != open || open >= song.patterns.size()) continue;
        const auto length = song.patterns[open].pattern.length();
        const auto span = clip.span(length);
        if (at < clip.start || at >= clip.start + span) continue;
        pass = at - clip.pattern_tick(at, length);
        break;
    }
    seekToTick(static_cast<double>(pass + static_cast<blokkily::Tick>(step) * 120));
}
