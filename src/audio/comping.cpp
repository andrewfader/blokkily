#include "blokkily/audio/comping.hpp"

#include "blokkily/audio/audio_clips.hpp"

#include <algorithm>
#include <cmath>

namespace blokkily {
namespace {

AudioClip* find(Song& song, AudioClipId id) {
    for (auto& clip : song.audio_clips)
        if (clip.id == id) return &clip;
    return nullptr;
}

bool overlap(const Song& song, const AudioClip& a, const AudioClip& b) {
    if (a.track != b.track) return false;
    const double a_end = audio_clip_end_tick(song, a);
    const double b_end = audio_clip_end_tick(song, b);
    return static_cast<double>(a.start) < b_end && static_cast<double>(b.start) < a_end;
}

} // namespace

std::vector<AudioClipId> take_stack(const Song& song, AudioClipId id) {
    std::vector<AudioClipId> stack;
    const AudioClip* clip = nullptr;
    for (const auto& candidate : song.audio_clips)
        if (candidate.id == id) clip = &candidate;
    if (clip == nullptr) return stack;
    for (const auto& other : song.audio_clips)
        if (other.id == id || overlap(song, *clip, other)) stack.push_back(other.id);
    std::sort(stack.begin(), stack.end());
    return stack;
}

bool choose_take(Song& song, AudioClipId id) {
    auto* chosen = find(song, id);
    if (chosen == nullptr) return false;
    chosen->comped_out = false;
    const auto picked = *chosen;
    for (auto& other : song.audio_clips)
        if (other.id != id && overlap(song, picked, other)) other.comped_out = true;
    return true;
}

int comp_recorded_takes(Song& song, const std::vector<AudioClipId>& recorded) {
    int comped = 0;
    for (const auto id : recorded) {
        auto* earlier = find(song, id);
        if (earlier == nullptr || earlier->comped_out) continue;
        const auto clip = *earlier;
        for (const auto later_id : recorded) {
            if (later_id <= id) continue;
            const auto* later = find(song, later_id);
            if (later != nullptr && overlap(song, clip, *later)) {
                find(song, id)->comped_out = true;
                ++comped;
                break;
            }
        }
    }
    return comped;
}

int split_takes(Song& song, std::size_t track, Tick at) {
    int cut = 0;
    const auto count = song.audio_clips.size();
    for (std::size_t index = 0; index < count; ++index) {
        auto& clip = song.audio_clips[index];
        if (clip.track != track || clip.file >= song.audio_files.size()) continue;
        if (!(static_cast<double>(clip.start) < static_cast<double>(at) &&
              static_cast<double>(at) < audio_clip_end_tick(song, clip)))
            continue;
        // Where the cut falls in the clip's own audio, in its file's frames.
        const auto rate = song.audio_files[clip.file].sample_rate;
        const double seconds = audio_clip_seconds_at(song, clip, static_cast<double>(at));
        const auto head = static_cast<std::uint64_t>(std::llround(seconds * rate));
        if (head == 0 || head >= clip.length_frames) continue;
        AudioClip tail = clip;
        tail.id = song.next_audio_clip_id();
        tail.start = at;
        tail.offset_frames = clip.offset_frames + head;
        tail.length_frames = clip.length_frames - head;
        tail.fade_in_frames = 0;
        tail.fade_out_frames = std::min(clip.fade_out_frames, tail.length_frames);
        auto& first = song.audio_clips[index];
        first.length_frames = head;
        first.fade_out_frames = 0;
        first.fade_in_frames = std::min(first.fade_in_frames, head);
        song.audio_clips.push_back(tail);
        ++cut;
    }
    return cut;
}

} // namespace blokkily
