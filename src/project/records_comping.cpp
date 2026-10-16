// Comping and punch (features/comping_and_punch.feature). Written only when
// a clip is comped out or punch is on, so a project without either saves the
// records it did before; a file without them plays every clip and records
// without punch.
//
//   comped_out <audio clip id>
//   punch on
//
// A clip id the file does not have, or a second record for one, is refused.

#include "records.hpp"

#include <algorithm>
#include <array>

namespace blokkily::project_io {
namespace {

void write_comping(const Project& project, WriteContext& context) {
    for (const auto& clip : project.song.audio_clips)
        if (clip.comped_out) context.out << "comped_out " << clip.id << '\n';
    if (project.song.loop.punch) context.out << "punch on\n";
}

bool parse_comped_out(const Fields& fields, ParseContext& context) {
    const auto id = fields.integer(1);
    if (!fields.count(2) || !id || *id < 0) return context.fail("malformed comped_out record");
    const auto clip = static_cast<AudioClipId>(*id);
    if (std::find(context.comped_out.begin(), context.comped_out.end(), clip) != context.comped_out.end())
        return context.fail("more than one comped_out record for a clip");
    context.comped_out.push_back(clip);
    return true;
}

bool parse_punch(const Fields& fields, ParseContext& context) {
    if (!fields.count(2) || fields.tokens[1] != "on") return context.fail("malformed punch record");
    if (context.punch) return context.fail("more than one punch record");
    context.punch = true;
    return true;
}

bool finish_comping(ParseContext& context) {
    auto& song = context.project.song;
    for (const auto id : context.comped_out) {
        const auto clip = std::find_if(song.audio_clips.begin(), song.audio_clips.end(),
                                       [id](const AudioClip& candidate) { return candidate.id == id; });
        if (clip == song.audio_clips.end())
            return context.fail("comped_out names a clip the file does not have");
        clip->comped_out = true;
    }
    song.loop.punch = context.punch;
    return true;
}

constexpr std::array comping_handlers{
    RecordHandler{"comped_out", parse_comped_out},
    RecordHandler{"punch", parse_punch},
};

} // namespace

const RecordModule& comping_records() {
    static const RecordModule module{"comping", write_comping, comping_handlers, finish_comping};
    return module;
}

} // namespace blokkily::project_io
