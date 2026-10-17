// How the score view writes the song (song.hpp, PatternSlot::clef and
// Section's key and meter). Written only where they differ from the
// default - a treble clef, no sharps or flats, the song's meter - so a song
// that never set them saves the records it did before, and a file without
// them loads with those defaults.
//
//   clef <pattern-index> <clef-name>
//   section-key <section-index> <fifths>
//   section-meter <section-index> <beats> <unit>
//   section-meter <section-index> free
//
// The clef is one of the eleven names notation::Clef gives (escaped, so
// "French violin" keeps its space), fifths are -7 to 7, and a meter is
// 1-64 beats of a power-of-two unit up to 32. Anything else is refused.

#include "records.hpp"

#include "blokkily/notation/clef.hpp"

#include <array>

namespace blokkily::project_io {
namespace {

bool known_clef(const std::string& name) {
    for (int kind = 0; kind <= static_cast<int>(notation::ClefKind::Percussion); ++kind)
        if (notation::Clef(static_cast<notation::ClefKind>(kind)).name() == name) return true;
    return false;
}

bool valid_unit(long long unit) {
    return unit == 1 || unit == 2 || unit == 4 || unit == 8 || unit == 16 || unit == 32;
}

void write_notation(const Project& project, WriteContext& context) {
    const auto& song = project.song;
    if (song.sections.empty()) return;
    for (std::size_t index = 0; index < song.patterns.size(); ++index)
        if (song.patterns[index].clef != "Treble")
            context.out << "clef " << index << ' ' << escape(song.patterns[index].clef) << '\n';
    for (std::size_t index = 0; index < song.sections.size(); ++index) {
        const auto& section = song.sections[index];
        if (section.key_fifths != 0) context.out << "section-key " << index << ' ' << section.key_fifths << '\n';
        if (section.unmetered)
            context.out << "section-meter " << index << " free\n";
        else if (section.meter_beats != 0)
            context.out << "section-meter " << index << ' ' << section.meter_beats << ' ' << section.meter_unit
                        << '\n';
    }
}

bool parse_clef(const Fields& fields, ParseContext& context) {
    const auto pattern = fields.integer(1);
    const auto name = fields.text(2);
    if (!fields.count(3) || !pattern || !name || *pattern < 0 || !known_clef(*name))
        return context.fail("malformed clef record");
    context.part_clefs.emplace_back(static_cast<std::size_t>(*pattern), *name);
    return true;
}

bool parse_section_key(const Fields& fields, ParseContext& context) {
    const auto section = fields.integer(1);
    const auto fifths = fields.integer(2);
    if (!fields.count(3) || !section || !fifths || *section < 0 || *fifths < -7 || *fifths > 7)
        return context.fail("malformed section-key record");
    context.section_keys.emplace_back(static_cast<std::size_t>(*section), static_cast<int>(*fifths));
    return true;
}

bool parse_section_meter(const Fields& fields, ParseContext& context) {
    const auto section = fields.integer(1);
    if (!section || *section < 0) return context.fail("malformed section-meter record");
    const auto index = static_cast<std::size_t>(*section);
    if (fields.count(3) && fields.text(2) == std::optional<std::string>{"free"}) {
        context.section_meters.emplace_back(index, 0, 4, true);
        return true;
    }
    const auto beats = fields.integer(2);
    const auto unit = fields.integer(3);
    if (!fields.count(4) || !beats || !unit || *beats < 1 || *beats > 64 || !valid_unit(*unit))
        return context.fail("malformed section-meter record");
    context.section_meters.emplace_back(index, static_cast<int>(*beats), static_cast<int>(*unit), false);
    return true;
}

bool finish_notation(ParseContext& context) {
    auto& song = context.project.song;
    for (const auto& [pattern, clef] : context.part_clefs) {
        if (pattern >= song.patterns.size()) return context.fail("clef record names a pattern that does not exist");
        song.patterns[pattern].clef = clef;
    }
    for (const auto& [section, fifths] : context.section_keys) {
        if (section >= song.sections.size())
            return context.fail("section-key record names a section that does not exist");
        song.sections[section].key_fifths = fifths;
    }
    for (const auto& [section, beats, unit, free] : context.section_meters) {
        if (section >= song.sections.size())
            return context.fail("section-meter record names a section that does not exist");
        auto& target = song.sections[section];
        target.unmetered = free;
        target.meter_beats = beats;
        target.meter_unit = unit;
    }
    context.part_clefs.clear();
    context.section_keys.clear();
    context.section_meters.clear();
    return true;
}

constexpr std::array notation_handlers{
    RecordHandler{"clef", parse_clef},
    RecordHandler{"section-key", parse_section_key},
    RecordHandler{"section-meter", parse_section_meter},
};

} // namespace

const RecordModule& notation_records() {
    static const RecordModule module{"notation", write_notation, notation_handlers, finish_notation};
    return module;
}

} // namespace blokkily::project_io
