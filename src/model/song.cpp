#include "blokkily/model/song.hpp"

#include "blokkily/sequencer/scheduler.hpp"

#include <algorithm>
#include <cmath>

namespace blokkily {

Tick Song::ticks_per_beat() const {
    return patterns.empty() ? 480 : patterns.front().pattern.ticks_per_beat();
}

Tick Song::length() const {
    Tick end = 0;
    for (const auto& clip : clips) {
        if (clip.track >= tracks.size() || clip.pattern >= patterns.size()) continue;
        const Tick span = patterns[clip.pattern].pattern.length() *
                          static_cast<Tick>(std::max<std::uint32_t>(1, clip.repeats));
        end = std::max(end, clip.start + span);
    }
    // An audio clip lasts a number of seconds, not ticks: where it ends in
    // the song depends on the tempo it plays through (plan C14).
    for (const auto& clip : audio_clips) {
        if (clip.track >= tracks.size() || clip.file >= audio_files.size()) continue;
        const auto rate = audio_files[clip.file].sample_rate;
        if (rate == 0) continue;
        // Warped or not (item 3.6): the one rule for where its audio sounds.
        const double tick = std::ceil(
            audio_clip_tick_at(*this, clip, static_cast<double>(clip.length_frames) / rate) -
            1e-6);
        if (tick > static_cast<double>(end)) end = static_cast<Tick>(tick);
    }
    if (end > 0) return end;
    return patterns.empty() ? 1920 : patterns.front().pattern.length();
}

bool Song::any_solo() const {
    return std::any_of(tracks.begin(), tracks.end(),
                       [](const Track& track) { return track.mix.solo; });
}

ScheduledEvents Song::arrange(std::size_t track, std::uint64_t seed) const {
    ScheduledEvents arranged;
    const Scheduler scheduler;
    for (const auto& clip : clips) {
        if (clip.track != track || clip.pattern >= patterns.size()) continue;
        const auto& source = patterns[clip.pattern].pattern;
        const auto repeats = std::max<std::uint32_t>(1, clip.repeats);
        for (std::uint32_t repeat = 0; repeat < repeats; ++repeat) {
            // Each repetition is the next loop of the pattern, so a step set to
            // play every other loop still alternates across the arrangement.
            const auto compiled = scheduler.render(source, repeat + 1, seed);
            const Tick offset = clip.start + static_cast<Tick>(repeat) * source.length();
            for (auto note : compiled.notes) {
                note.start += offset;
                arranged.notes.push_back(note);
            }
            for (auto parameter : compiled.parameters) {
                parameter.start += offset;
                arranged.parameters.push_back(parameter);
            }
        }
    }
    const auto by_start = [](const auto& a, const auto& b) { return a.start < b.start; };
    std::stable_sort(arranged.notes.begin(), arranged.notes.end(), by_start);
    std::stable_sort(arranged.parameters.begin(), arranged.parameters.end(), by_start);
    return arranged;
}

} // namespace blokkily
