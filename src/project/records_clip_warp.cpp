// Clip warp (item 3.6): how an audio clip is stretched and pitched.
//
//   clipwarp <clip-id> <follow 0|1> <source-bpm> <ratio> <semitones> <cents>
//
// Written after the audio records, only for a clip whose warp differs from
// the default, so a clip that is not warped saves exactly as before. A
// clipwarp record names a clip an audioclip record before it defines, once.
// The values are range-checked by Song::consistent once the file is read.

#include "records.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace blokkily::project_io {

namespace {

void write_clip_warp(const Project& project, WriteContext& context) {
    for (const auto& clip : project.song.audio_clips) {
        if (clip.warp == ClipWarp{}) continue;
        context.out << "clipwarp " << clip.id << ' ' << (clip.warp.follow_tempo ? 1 : 0) << ' '
                    << number(clip.warp.source_bpm) << ' ' << number(clip.warp.ratio) << ' '
                    << clip.warp.semitones << ' ' << number(clip.warp.cents) << '\n';
    }
}

bool parse_clipwarp(const Fields& fields, ParseContext& context) {
    const auto id = fields.integer(1);
    const auto follow = fields.integer(2);
    const auto bpm = fields.real(3);
    const auto ratio = fields.real(4);
    const auto semitones = fields.integer(5);
    const auto cents = fields.real(6);
    if (!fields.count(7) || !id || !follow || !bpm || !ratio || !semitones || !cents ||
        *id <= 0 || (*follow != 0 && *follow != 1) ||
        *semitones < std::numeric_limits<std::int32_t>::min() ||
        *semitones > std::numeric_limits<std::int32_t>::max())
        return context.fail("malformed clipwarp record");
    auto& clips = context.project.song.audio_clips;
    const auto found = std::find_if(clips.begin(), clips.end(), [&](const AudioClip& clip) {
        return clip.id == static_cast<AudioClipId>(*id);
    });
    if (found == clips.end())
        return context.fail("clipwarp names audio clip " + std::to_string(*id) +
                            ", which no audioclip record before it defines");
    if (found->warp != ClipWarp{})
        return context.fail("two clipwarp records for audio clip " + std::to_string(*id));
    found->warp.follow_tempo = *follow == 1;
    found->warp.source_bpm = *bpm;
    found->warp.ratio = *ratio;
    found->warp.semitones = static_cast<std::int32_t>(*semitones);
    found->warp.cents = *cents;
    if (found->warp == ClipWarp{})
        return context.fail("clipwarp record for audio clip " + std::to_string(*id) +
                            " changes nothing");
    return true;
}

constexpr std::array clip_warp_handlers{
    RecordHandler{"clipwarp", parse_clipwarp},
};

} // namespace

const RecordModule& clip_warp_records() {
    static const RecordModule module{"clip_warp", write_clip_warp, clip_warp_handlers, nullptr};
    return module;
}

} // namespace blokkily::project_io
