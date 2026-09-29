#include "blokkily/model/song.hpp"

#include "blokkily/sequencer/scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace blokkily {

Tick Song::ticks_per_beat() const {
    return patterns.empty() ? 480 : patterns.front().pattern.ticks_per_beat();
}

Tick Song::length() const {
    Tick end = 0;
    for (const auto& clip : clips) {
        if (clip.track >= tracks.size() || clip.pattern >= patterns.size()) continue;
        const Tick span = clip.span(patterns[clip.pattern].pattern.length());
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

bool Song::print_take(std::size_t track, std::size_t pattern, Tick start, Tick end,
                      std::uint32_t cycle) {
    if (track >= tracks.size() || pattern >= patterns.size() || start < 0 || end <= start)
        return false;
    const Tick length = patterns[pattern].pattern.length();
    if (length <= 0) return false;
    cycle = std::max<std::uint32_t>(1, cycle);

    // Room for the take on its track.
    std::vector<Clip> kept;
    kept.reserve(clips.size() + 4);
    for (auto clip : clips) {
        if (clip.track != track || clip.pattern >= patterns.size()) {
            kept.push_back(clip);
            continue;
        }
        const Tick own = patterns[clip.pattern].pattern.length();
        const Tick clip_end = clip.start + clip.span(own);
        if (clip_end <= start || clip.start >= end) {
            kept.push_back(clip);
            continue;
        }
        if (clip.start >= start) continue;
        // Runs into the take: cut where it begins.
        const Tick cut = start - clip.start;
        clip.repeats = static_cast<std::uint32_t>((cut + own - 1) / own);
        clip.length = cut == own * static_cast<Tick>(clip.repeats) ? 0 : cut;
        kept.push_back(clip);
    }
    // The take, a cycle of loops at a time, so each clip's loops number from
    // 1 exactly as the launcher's did. A pattern whose loops are all alike
    // prints as one clip.
    const Tick per_clip = cycle > 1 ? static_cast<Tick>(cycle) : std::numeric_limits<std::uint32_t>::max();
    for (Tick at = start; at < end;) {
        const auto loops = static_cast<std::uint32_t>(
            std::min<Tick>(per_clip, (end - at + length - 1) / length));
        const Tick span = length * static_cast<Tick>(loops);
        kept.push_back({track, pattern, at, loops, at + span > end ? end - at : 0});
        at += span;
    }
    clips = std::move(kept);
    return true;
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
        // A cut clip (Clip::length) plays nothing from the cut on and lets go
        // of what is still sounding there.
        const bool cut = clip.length > 0 && clip.span(source.length()) == clip.length;
        const Tick end = clip.start + clip.span(source.length());
        for (std::uint32_t repeat = 0; repeat < repeats; ++repeat) {
            const Tick offset = clip.start + static_cast<Tick>(repeat) * source.length();
            if (offset >= end) break;
            // Each repetition is the next loop of the pattern, so a step set to
            // play every other loop still alternates across the arrangement.
            const auto compiled = scheduler.render(source, repeat + 1, seed);
            for (auto note : compiled.notes) {
                note.start += offset;
                if (cut && note.start >= end) continue;
                if (cut && note.start + note.duration > end) note.duration = end - note.start;
                arranged.notes.push_back(note);
            }
            for (auto parameter : compiled.parameters) {
                parameter.start += offset;
                if (cut && parameter.start >= end) continue;
                arranged.parameters.push_back(parameter);
            }
            for (auto control : compiled.continuous) {
                control.start += offset;
                if (cut && control.start >= end) continue;
                arranged.continuous.push_back(control);
            }
        }
    }
    const auto by_start = [](const auto& a, const auto& b) { return a.start < b.start; };
    std::stable_sort(arranged.notes.begin(), arranged.notes.end(), by_start);
    std::stable_sort(arranged.parameters.begin(), arranged.parameters.end(), by_start);
    std::stable_sort(arranged.continuous.begin(), arranged.continuous.end(), by_start);
    return arranged;
}

} // namespace blokkily
