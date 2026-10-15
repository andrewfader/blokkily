// The arrangement loop. Written only when it differs from the default
// (off, no region), so a project that never looped saves the records it did
// before. A file without the record loads with the loop off.
//
//   loop <on|off> <start-tick> <end-tick>
//
// At most one record. The end must be greater than the start; ticks are
// non-negative. Anything else is refused rather than clamped.

#include "records.hpp"

#include <array>

namespace blokkily::project_io {
namespace {

void write_loop(const Project& project, WriteContext& context) {
    const auto& loop = project.song.loop;
    if (loop == ArrangementLoop{}) return;
    context.out << "loop " << (loop.enabled ? "on" : "off") << ' ' << loop.start << ' '
                << loop.end << '\n';
}

bool parse_loop(const Fields& fields, ParseContext& context) {
    if (!fields.count(4)) return context.fail("malformed loop record");
    if (context.loop) return context.fail("more than one loop record");
    const std::string& state = fields.tokens[1];
    const auto start = fields.integer(2);
    const auto end = fields.integer(3);
    if ((state != "on" && state != "off") || !start || !end || *start < 0 || *end < 0)
        return context.fail("malformed loop record");
    ArrangementLoop loop;
    loop.enabled = state == "on";
    loop.start = *start;
    loop.end = *end;
    if (loop.enabled && loop.end <= loop.start)
        return context.fail("loop end must be after loop start");
    context.loop = loop;
    return true;
}

bool finish_loop(ParseContext& context) {
    if (context.loop) context.project.song.loop = *context.loop;
    context.loop.reset();
    return true;
}

constexpr std::array loop_handlers{
    RecordHandler{"loop", parse_loop},
};

} // namespace

const RecordModule& loop_records() {
    static const RecordModule module{"loop", write_loop, loop_handlers, finish_loop};
    return module;
}

} // namespace blokkily::project_io
