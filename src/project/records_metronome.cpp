// The metronome and count-in settings (item 3.7, decision 14).
//
//   metronome <on|off> <level-db> <count-in-bars>
//
// Written only when the settings differ from the defaults (off, -6 dB, no
// count-in), so a project that never touched the click saves exactly the
// records it did before. A file without the record loads with the defaults.
// At most one metronome record; the level is -60 to +6 dB and the count-in
// 0 to 4 bars, and anything else is refused rather than clamped.

#include "records.hpp"

#include <array>

namespace blokkily::project_io {

namespace {

void write_metronome(const Project& project, WriteContext& context) {
    const auto& metronome = project.song.metronome;
    if (metronome == MetronomeSettings{}) return;
    context.out << "metronome " << (metronome.enabled ? "on" : "off") << ' '
                << number(metronome.level_db) << ' ' << metronome.count_in_bars << '\n';
}

bool parse_metronome(const Fields& fields, ParseContext& context) {
    if (!fields.count(4)) return context.fail("malformed metronome record");
    if (context.metronome) return context.fail("more than one metronome record");
    const std::string& state = fields.tokens[1];
    const auto level = fields.real(2);
    const auto bars = fields.integer(3);
    if ((state != "on" && state != "off") || !level || !bars || *bars < 0 ||
        *bars > MetronomeSettings::maximum_count_in_bars)
        return context.fail("malformed metronome record");
    MetronomeSettings settings;
    settings.enabled = state == "on";
    settings.level_db = *level;
    settings.count_in_bars = static_cast<int>(*bars);
    if (!settings.valid())
        return context.fail("metronome level must be -60 to +6 dB and the count-in 0 to 4 bars");
    context.metronome = settings;
    return true;
}

// Runs after the core module has built the song, which it builds afresh.
bool finish_metronome(ParseContext& context) {
    if (context.metronome) context.project.song.metronome = *context.metronome;
    context.metronome.reset();
    return true;
}

constexpr std::array metronome_handlers{
    RecordHandler{"metronome", parse_metronome},
};

} // namespace

const RecordModule& metronome_records() {
    static const RecordModule module{"metronome", write_metronome, metronome_handlers,
                                     finish_metronome};
    return module;
}

} // namespace blokkily::project_io
