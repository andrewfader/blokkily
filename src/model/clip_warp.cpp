// Clip warp in the model (item 3.6): the one rule for where a clip's audio
// sounds in the song, stretched or not. Song::length(), the clip lane, trims,
// fades and the rendition planner (clip_warp.hpp) all place a clip's frames
// through audio_clip_tick_at, so none of them can disagree about where a warped
// clip ends.

#include "blokkily/model/song.hpp"

#include <cmath>

namespace blokkily {

double ClipWarp::pitch_scale() const noexcept {
    return std::exp2((static_cast<double>(semitones) + cents / 100.0) / 12.0);
}

bool ClipWarp::valid() const noexcept {
    if (!std::isfinite(source_bpm) || !std::isfinite(ratio) || !std::isfinite(cents))
        return false;
    if (source_bpm != 0.0 && (source_bpm < minimum_source_bpm || source_bpm > maximum_source_bpm))
        return false;
    return ratio >= minimum_ratio && ratio <= maximum_ratio &&
           semitones >= -maximum_semitones && semitones <= maximum_semitones &&
           cents >= -maximum_cents && cents <= maximum_cents;
}

double audio_clip_tick_at(const Song& song, const AudioClip& clip, double seconds) {
    const auto resolution = song.ticks_per_beat();
    const double start = static_cast<double>(clip.start);
    if (clip.warp.follows()) {
        // A beat of the recording is `ratio` beats of the song, whatever
        // tempo the song is at there.
        return start + seconds * clip.warp.source_bpm / 60.0 * static_cast<double>(resolution) *
                           clip.warp.ratio;
    }
    const double ratio = clip.warp.ratio > 0.0 ? clip.warp.ratio : 1.0;
    return song.tempo.tick_at_seconds(song.tempo.seconds_at(clip.start, resolution) +
                                          seconds * ratio,
                                      resolution);
}

double audio_clip_seconds_at(const Song& song, const AudioClip& clip, double tick) {
    const auto resolution = song.ticks_per_beat();
    const double start = static_cast<double>(clip.start);
    if (clip.warp.follows()) {
        return (tick - start) / (static_cast<double>(resolution) * clip.warp.ratio) * 60.0 /
               clip.warp.source_bpm;
    }
    const double ratio = clip.warp.ratio > 0.0 ? clip.warp.ratio : 1.0;
    // Before the song starts time does not run: seconds_at_tick counts ticks
    // before 0 as 0, so extrapolate there at the first tempo.
    double seconds = song.tempo.seconds_at_tick(std::max(0.0, tick), resolution);
    if (tick < 0.0)
        seconds += tick * 60.0 / (song.tempo.bpm_at(0) * static_cast<double>(resolution));
    return (seconds - song.tempo.seconds_at(clip.start, resolution)) / ratio;
}

} // namespace blokkily
