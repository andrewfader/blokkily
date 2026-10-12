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

void AppController::setFollowPlayback(bool follow) {
    if (follow == follow_playback_) return;
    follow_playback_ = follow;
    emit followPlaybackChanged();
    followPlayhead();
}

void AppController::followPlayhead() {
    if (!follow_playback_ || !song_ || !transport_ || !transport_->playing()) return;
    const auto& song = song_->song();
    const auto track = static_cast<std::size_t>(std::max(0, song_->selectedTrack()));
    const auto at = static_cast<blokkily::Tick>(std::floor(transport_->tick()));
    for (const auto& clip : song.clips) {
        if (clip.track != track || clip.pattern >= song.patterns.size()) continue;
        const auto& part = song.patterns[clip.pattern];
        const auto length = part.pattern.length();
        if (length <= 0 || at < clip.start || at >= clip.start + clip.span(length)) continue;
        // Opening a pattern is not an edit: no step of history, nothing heard
        // differently.
        if (static_cast<int>(part.section) != song_->currentSection())
            song_->selectSection(static_cast<int>(part.section));
        return;
    }
}

// Project the engine-following transport into the open pattern's clip, including
// repeats and trimmed offsets. No second playback clock or pattern is kept.
double AppController::patternPlayStep() const {
    if (!song_ || !transport_) return -1.0;
    const auto& song = song_->song();
    const auto open = static_cast<std::size_t>(std::max(0, song_->currentPattern()));
    if (open >= song.patterns.size()) return -1.0;
    const auto length = song.patterns[open].pattern.length();
    if (length <= 0) return -1.0;
    const double tick = transport_->tick();
    const auto at = static_cast<blokkily::Tick>(std::floor(tick));
    for (const auto& clip : song.clips) {
        if (clip.track != static_cast<std::size_t>(song_->selectedTrack()) ||
            clip.pattern != open || at < clip.start || at >= clip.start + clip.span(length)) continue;
        return (static_cast<double>(clip.pattern_tick(at, length)) + tick - at) / 120.0;
    }
    // A resting cursor still supports locating within an unplaced pattern.
    if (transport_->playing()) return -1.0;
    return std::fmod(transport_->barStepFraction(), static_cast<double>(length) / 120.0);
}
