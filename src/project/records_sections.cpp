// Sections (song.hpp, Section): the patterns grouped into the sections a
// producer names, one part per track.
//
//   section <name>
//   part <pattern> <section> <track>
//
// Sections are written in order, then one part record per pattern, and only
// for a song that has sections. A file without them loads without them and
// saves back byte for byte; the application gives such a song its sections
// when it opens it (Song::adopt_sections). A file with them must name a part
// for every pattern; the song's consistency check then
// requires one part per section and track.

#include "records.hpp"

#include <array>

namespace blokkily::project_io {

namespace {

void write_sections(const Project& project, WriteContext& context) {
    const auto& song = project.song;
    if (song.sections.empty()) return;
    for (const auto& section : song.sections)
        context.out << "section " << escape(section.name) << '\n';
    for (std::size_t index = 0; index < song.patterns.size(); ++index)
        context.out << "part " << index << ' ' << song.patterns[index].section << ' '
                    << song.patterns[index].track << '\n';
}

bool parse_section(const Fields& fields, ParseContext& context) {
    const auto name = fields.text(1);
    if (!fields.count(2) || !name) return context.fail("malformed section record");
    context.sections.push_back({*name});
    return true;
}

bool parse_part(const Fields& fields, ParseContext& context) {
    const auto pattern = fields.integer(1);
    const auto section = fields.integer(2);
    const auto track = fields.integer(3);
    if (!fields.count(4) || !pattern || !section || !track || *pattern < 0 || *section < 0 ||
        *track < 0)
        return context.fail("malformed part record");
    context.parts.push_back({static_cast<std::size_t>(*pattern),
                             static_cast<std::size_t>(*section),
                             static_cast<std::size_t>(*track)});
    return true;
}

bool finish_sections(ParseContext& context) {
    auto& song = context.project.song;
    if (context.sections.empty()) {
        if (!context.parts.empty()) return context.fail("part record without a section");
        return true;
    }
    if (context.parts.size() != song.patterns.size())
        return context.fail("every pattern needs exactly one part record");
    std::vector<bool> seen(song.patterns.size(), false);
    for (const auto& part : context.parts) {
        if (part.pattern >= song.patterns.size() || seen[part.pattern] ||
            part.section >= context.sections.size())
            return context.fail("part record names a pattern or section that does not exist");
        seen[part.pattern] = true;
        song.patterns[part.pattern].section = part.section;
        song.patterns[part.pattern].track = part.track;
    }
    song.sections = std::move(context.sections);
    context.sections.clear();
    context.parts.clear();
    return true;
}

constexpr std::array sections_handlers{
    RecordHandler{"section", parse_section},
    RecordHandler{"part", parse_part},
};

} // namespace

const RecordModule& sections_records() {
    static const RecordModule module{"sections", write_sections, sections_handlers,
                                     finish_sections};
    return module;
}

} // namespace blokkily::project_io
