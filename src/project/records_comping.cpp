// Comping and punch (features/comping_and_punch.feature). Written only when
// a clip is comped out or punch is on, so a project without either saves the
// records it did before; a file without them plays every clip and records
// without punch.
//
//   comped_out <audio clip id>
//   punch on
//   comped_out_clip <clip index>     a MIDI take kept but not played
//   midi_takes off                   looped MIDI passes merge, not stack
//
// A clip id or index the file does not have, or a second record for one, is
// refused.

#include "records.hpp"

#include <algorithm>
#include <array>

namespace blokkily::project_io {
namespace {

void write_comping(const Project& project, WriteContext& context) {
    for (const auto& clip : project.song.audio_clips)
        if (clip.comped_out) context.out << "comped_out " << clip.id << '\n';
    if (project.song.loop.punch) context.out << "punch on\n";
    for (std::size_t index = 0; index < project.song.clips.size(); ++index)
        if (project.song.clips[index].comped_out) context.out << "comped_out_clip " << index << '\n';
    if (!project.song.loop.midi_takes) context.out << "midi_takes off\n";
}

// An index record: once each, a non-negative integer.
bool parse_index(const Fields& fields, ParseContext& context, std::vector<std::size_t>& into,
                 const char* name) {
    const auto index = fields.integer(1);
    if (!fields.count(2) || !index || *index < 0)
        return context.fail(std::string("malformed ") + name + " record");
    const auto value = static_cast<std::size_t>(*index);
    if (std::find(into.begin(), into.end(), value) != into.end())
        return context.fail(std::string("more than one ") + name + " record for one index");
    into.push_back(value);
    return true;
}

bool parse_comped_out_clip(const Fields& fields, ParseContext& context) {
    return parse_index(fields, context, context.comped_out_clips, "comped_out_clip");
}

bool parse_midi_takes(const Fields& fields, ParseContext& context) {
    if (!fields.count(2) || fields.tokens[1] != "off") return context.fail("malformed midi_takes record");
    if (context.midi_merge) return context.fail("more than one midi_takes record");
    context.midi_merge = true;
    return true;
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
    for (const auto index : context.comped_out_clips) {
        if (index >= song.clips.size())
            return context.fail("comped_out_clip names a clip the file does not have");
        song.clips[index].comped_out = true;
    }
    song.loop.midi_takes = !context.midi_merge;
    return true;
}

constexpr std::array comping_handlers{
    RecordHandler{"comped_out", parse_comped_out},
    RecordHandler{"punch", parse_punch},
    RecordHandler{"comped_out_clip", parse_comped_out_clip},
    RecordHandler{"midi_takes", parse_midi_takes},
};

} // namespace

const RecordModule& comping_records() {
    static const RecordModule module{"comping", write_comping, comping_handlers, finish_comping};
    return module;
}

} // namespace blokkily::project_io
