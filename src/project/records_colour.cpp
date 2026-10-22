// Track colours. Written only for a track given a colour of its own, so a
// project that never coloured a track saves the records it did before. A
// file without the record draws every track in the window's own colours.
//
//   track_colour <track-index> <#rrggbb>
//
// At most one record per track. A colour that is not six hex digits after
// '#' is refused.

#include "records.hpp"

#include <array>
#include <cctype>

namespace blokkily::project_io {
namespace {

bool is_colour(const std::string& text) {
    if (text.size() != 7 || text[0] != '#') return false;
    for (std::size_t index = 1; index < text.size(); ++index)
        if (std::isxdigit(static_cast<unsigned char>(text[index])) == 0) return false;
    return true;
}

void write_colours(const Project& project, WriteContext& context) {
    for (std::size_t index = 0; index < project.song.tracks.size(); ++index) {
        const auto& colour = project.song.tracks[index].colour;
        if (colour.empty()) continue;
        context.out << "track_colour " << index << ' ' << colour << '\n';
    }
}

bool parse_colour(const Fields& fields, ParseContext& context) {
    if (!fields.count(3)) return context.fail("malformed track_colour record");
    const auto track = fields.integer(1);
    const std::string colour = fields.tokens[2];
    if (!track || *track < 0 || !is_colour(colour)) return context.fail("malformed track_colour record");
    const auto index = static_cast<std::size_t>(*track);
    for (const auto& [seen, unused] : context.track_colours)
        if (seen == index) return context.fail("more than one track_colour record for a track");
    context.track_colours.emplace_back(index, colour);
    return true;
}

bool finish_colours(ParseContext& context) {
    auto& tracks = context.project.song.tracks;
    for (const auto& [index, colour] : context.track_colours) {
        if (index >= tracks.size()) return context.fail("track_colour record names a track the file does not have");
        tracks[index].colour = colour;
    }
    context.track_colours.clear();
    return true;
}

constexpr std::array colour_handlers{
    RecordHandler{"track_colour", parse_colour},
};

} // namespace

const RecordModule& colour_records() {
    static const RecordModule module{"colour", write_colours, colour_handlers, finish_colours};
    return module;
}

} // namespace blokkily::project_io
