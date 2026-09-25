// Audio files and audio clips (plan §F-E, item 1.5).
//
//   audiofile <path> <frames> <sample-rate> <channels>
//   audioclip <id> <track> <file> <start> <offset-frames> <length-frames>
//             <gain-db> <fade-in-frames> <fade-out-frames>
//
// Files are numbered by the order of their records. A path inside the project
// folder is stored relative to it (paths.hpp). Cross-references are checked by
// Song::consistent once the whole file is read.

#include "blokkily/project/paths.hpp"
#include "records.hpp"

#include <array>
#include <limits>

namespace blokkily::project_io {

namespace {

void write_audio(const Project& project, WriteContext& context) {
    auto& out = context.out;
    for (const auto& file : project.song.audio_files)
        out << "audiofile "
            << escape(to_project_relative(file.path, context.base_dir).generic_string()) << ' '
            << file.frames << ' ' << file.sample_rate << ' ' << file.channels << '\n';
    for (const auto& clip : project.song.audio_clips)
        out << "audioclip " << clip.id << ' ' << clip.track << ' ' << clip.file << ' '
            << clip.start << ' ' << clip.offset_frames << ' ' << clip.length_frames << ' '
            << number(clip.gain_db) << ' ' << clip.fade_in_frames << ' ' << clip.fade_out_frames
            << '\n';
}

bool parse_audiofile(const Fields& fields, ParseContext& context) {
    const auto path = fields.text(1);
    const auto frames = fields.integer(2);
    const auto rate = fields.integer(3);
    const auto channels = fields.integer(4);
    if (!fields.count(5) || !path || path->empty() || !frames || !rate || !channels ||
        *frames <= 0 || *rate <= 0 || *rate > std::numeric_limits<std::uint32_t>::max() ||
        *channels <= 0 || *channels > std::numeric_limits<std::uint16_t>::max())
        return context.fail("malformed audiofile record");
    context.project.song.audio_files.push_back(
        {resolve_project_path(std::filesystem::path(*path), context.base_dir),
         static_cast<std::uint64_t>(*frames), static_cast<std::uint32_t>(*rate),
         static_cast<std::uint16_t>(*channels)});
    return true;
}

bool parse_audioclip(const Fields& fields, ParseContext& context) {
    const auto id = fields.integer(1);
    const auto track = fields.integer(2);
    const auto file = fields.integer(3);
    const auto start = fields.integer(4);
    const auto offset = fields.integer(5);
    const auto length = fields.integer(6);
    const auto gain = fields.real(7);
    const auto fade_in = fields.integer(8);
    const auto fade_out = fields.integer(9);
    if (!fields.count(10) || !id || !track || !file || !start || !offset || !length || !gain ||
        !fade_in || !fade_out || *id <= 0 || *track < 0 || *file < 0 || *start < 0 ||
        *offset < 0 || *length <= 0 || *fade_in < 0 || *fade_out < 0)
        return context.fail("malformed audioclip record");
    AudioClip clip;
    clip.id = static_cast<AudioClipId>(*id);
    clip.track = static_cast<std::size_t>(*track);
    clip.file = static_cast<std::size_t>(*file);
    clip.start = *start;
    clip.offset_frames = static_cast<std::uint64_t>(*offset);
    clip.length_frames = static_cast<std::uint64_t>(*length);
    clip.gain_db = *gain;
    clip.fade_in_frames = static_cast<std::uint64_t>(*fade_in);
    clip.fade_out_frames = static_cast<std::uint64_t>(*fade_out);
    context.project.song.audio_clips.push_back(clip);
    return true;
}

constexpr std::array audio_handlers{
    RecordHandler{"audiofile", parse_audiofile},
    RecordHandler{"audioclip", parse_audioclip},
};

} // namespace

const RecordModule& audio_records() {
    static const RecordModule module{"audio", write_audio, audio_handlers, nullptr};
    return module;
}

} // namespace blokkily::project_io
