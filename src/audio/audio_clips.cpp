#include "blokkily/audio/audio_clips.hpp"

#include <algorithm>
#include <cmath>

namespace blokkily {

std::size_t ClipAssetReport::missing_count() const {
    return static_cast<std::size_t>(std::count(missing.begin(), missing.end(), true));
}

AudioAssets load_clip_assets(const Song& song, AudioAssetCache& cache, double engine_rate,
                             ClipAssetReport* report) {
    AudioAssets assets(song.audio_files.size());
    if (report != nullptr) {
        report->missing.assign(song.audio_files.size(), false);
        report->reasons.assign(song.audio_files.size(), std::string{});
    }
    for (std::size_t index = 0; index < song.audio_files.size(); ++index) {
        const auto& file = song.audio_files[index];
        std::string error;
        assets[index] = cache.load(file.path, engine_rate,
                                   AudioFileInfo{file.frames, file.sample_rate, file.channels},
                                   &error);
        if (!assets[index] && report != nullptr) {
            report->missing[index] = true;
            report->reasons[index] = error.empty() ? file.path.string() + " is missing" : error;
        }
    }
    return assets;
}

double audio_clip_end_tick(const Song& song, const AudioClip& clip) {
    const auto resolution = song.ticks_per_beat();
    const double start = static_cast<double>(clip.start);
    if (clip.file >= song.audio_files.size() || song.audio_files[clip.file].sample_rate == 0)
        return start;
    const double seconds = song.tempo.seconds_at(clip.start, resolution) +
                           static_cast<double>(clip.length_frames) /
                               song.audio_files[clip.file].sample_rate;
    return song.tempo.tick_at_seconds(seconds, resolution);
}

namespace {
// Seconds at a fractional tick: the tempo map answers for whole ticks, and the
// part of a tick left over is at the tempo in effect there.
double seconds_at(const Song& song, double tick) {
    const auto resolution = song.ticks_per_beat();
    const double whole = std::floor(std::max(0.0, tick));
    const double seconds = song.tempo.seconds_at(static_cast<Tick>(whole), resolution);
    const double bpm = song.tempo.bpm_at(static_cast<Tick>(whole));
    return seconds + (std::max(0.0, tick) - whole) * 60.0 / (bpm * static_cast<double>(resolution));
}
} // namespace

double frames_between(const Song& song, double from, double to, std::uint32_t rate) {
    return (seconds_at(song, to) - seconds_at(song, from)) * static_cast<double>(rate);
}

std::size_t add_audio_file(Song& song, const AudioFileRef& file) {
    for (std::size_t index = 0; index < song.audio_files.size(); ++index)
        if (song.audio_files[index] == file) return index;
    song.audio_files.push_back(file);
    return song.audio_files.size() - 1;
}

} // namespace blokkily
