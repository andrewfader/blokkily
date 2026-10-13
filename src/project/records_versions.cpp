// The song's notes and the project's saved versions:
//
//   notes <text>
//   version <name> <saved-at> <snapshot>
//
// `text`, `name` and `snapshot` are escaped (one token each); `saved-at` is
// seconds since 1970, UTC; `snapshot` is the project file text of the song as
// it was, with no versions of its own. notes is written only for a song with
// notes, and version once per version, oldest first, so a file with neither
// is unchanged.

#include "records.hpp"

#include <array>

namespace blokkily::project_io {
namespace {

void write_versions(const Project& project, WriteContext& context) {
    auto& out = context.out;
    if (!project.song.notes.empty()) out << "notes " << escape(project.song.notes) << '\n';
    for (const auto& version : project.versions)
        out << "version " << escape(version.name) << ' ' << version.saved_at << ' '
            << escape(version.snapshot) << '\n';
}

bool parse_notes(const Fields& fields, ParseContext& context) {
    if (!fields.count(2)) return context.fail("malformed notes record");
    if (context.notes) return context.fail("a project has one notes record");
    const auto text = fields.text(1);
    if (!text) return context.fail("malformed notes record");
    context.notes = *text;
    return true;
}

bool parse_version(const Fields& fields, ParseContext& context) {
    const auto name = fields.text(1);
    const auto saved_at = fields.integer(2);
    const auto snapshot = fields.text(3);
    if (!fields.count(4) || !name || !saved_at || !snapshot || *saved_at < 0)
        return context.fail("malformed version record");
    context.project.versions.push_back({*name, *saved_at, *snapshot});
    return true;
}

bool finish_versions(ParseContext& context) {
    if (context.notes) context.project.song.notes = *context.notes;
    return true;
}

constexpr std::array version_handlers{
    RecordHandler{"notes", parse_notes},
    RecordHandler{"version", parse_version},
};

} // namespace

const RecordModule& version_records() {
    static const RecordModule module{"versions", write_versions, version_handlers,
                                     finish_versions};
    return module;
}

} // namespace blokkily::project_io
