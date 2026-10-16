// Track freeze (features/track_freeze.feature). Written only for a frozen
// track and its freeze clip, so a project without either saves the records
// it did before.
//
//   frozen <track>
//   freeze_clip <audio clip id>
//
// A track or clip the file does not have, or a second record for one, is
// refused; so is a frozen track without a freeze clip.

#include "records.hpp"

#include <algorithm>
#include <array>

namespace blokkily::project_io {
namespace {

void write_freeze(const Project& project, WriteContext& context) {
    for (std::size_t index = 0; index < project.song.tracks.size(); ++index)
        if (project.song.tracks[index].frozen) context.out << "frozen " << index << '\n';
    for (const auto& clip : project.song.audio_clips)
        if (clip.freeze) context.out << "freeze_clip " << clip.id << '\n';
}

bool parse_frozen(const Fields& fields, ParseContext& context) {
    const auto track = fields.integer(1);
    if (!fields.count(2) || !track || *track < 0) return context.fail("malformed frozen record");
    const auto index = static_cast<std::size_t>(*track);
    if (std::find(context.frozen_tracks.begin(), context.frozen_tracks.end(), index) != context.frozen_tracks.end())
        return context.fail("more than one frozen record for a track");
    context.frozen_tracks.push_back(index);
    return true;
}

bool parse_freeze_clip(const Fields& fields, ParseContext& context) {
    const auto id = fields.integer(1);
    if (!fields.count(2) || !id || *id < 0) return context.fail("malformed freeze_clip record");
    const auto clip = static_cast<AudioClipId>(*id);
    if (std::find(context.freeze_clips.begin(), context.freeze_clips.end(), clip) != context.freeze_clips.end())
        return context.fail("more than one freeze_clip record for a clip");
    context.freeze_clips.push_back(clip);
    return true;
}

bool finish_freeze(ParseContext& context) {
    auto& song = context.project.song;
    for (const auto index : context.frozen_tracks) {
        if (index >= song.tracks.size()) return context.fail("frozen names a track the file does not have");
        song.tracks[index].frozen = true;
    }
    for (const auto id : context.freeze_clips) {
        const auto clip = std::find_if(song.audio_clips.begin(), song.audio_clips.end(),
                                       [id](const AudioClip& candidate) { return candidate.id == id; });
        if (clip == song.audio_clips.end())
            return context.fail("freeze_clip names a clip the file does not have");
        clip->freeze = true;
    }
    for (const auto index : context.frozen_tracks)
        if (std::none_of(song.audio_clips.begin(), song.audio_clips.end(),
                         [index](const AudioClip& clip) { return clip.track == index && clip.freeze; }))
            return context.fail("a frozen track has no freeze clip");
    return true;
}

constexpr std::array freeze_handlers{
    RecordHandler{"frozen", parse_frozen},
    RecordHandler{"freeze_clip", parse_freeze_clip},
};

} // namespace

const RecordModule& freeze_records() {
    static const RecordModule module{"freeze", write_freeze, freeze_handlers, finish_freeze};
    return module;
}

} // namespace blokkily::project_io
