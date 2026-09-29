// Controller movements in patterns (phase 2, wave 4.1): the pitch wheel,
// control changes and channel pressure a take recorded, one additive record
// each, after the patterns they belong to:
//
//   control <pattern> <tick> bend <value 0..16383>
//   control <pattern> <tick> cc <controller 0..119, not 64> <value 0..127>
//   control <pattern> <tick> pressure <value 0..127>
//   control <pattern> <tick> poly <key 0..127> <value 0..127>
//
// Poly pressure (one key's aftertouch) was added after the others: a file
// that holds one is refused by a reader from before it, whose kinds end at
// pressure, rather than played without it.
//
// A file without them (every file written before wave 4.1) loads with no
// controller movements, and plays exactly as it did. They are written in the
// pattern's own order, so a save is byte-stable.

#include "records.hpp"

#include <array>

namespace blokkily::project_io {
namespace {

void write_continuous(const Project& project, WriteContext& context) {
    auto& out = context.out;
    for (std::size_t index = 0; index < project.song.patterns.size(); ++index) {
        for (const auto& control : project.song.patterns[index].pattern.continuous()) {
            out << "control " << index << ' ' << control.tick << ' ';
            switch (control.kind) {
            case ContinuousEvent::Kind::pitch_bend: out << "bend " << control.value; break;
            case ContinuousEvent::Kind::control_change:
                out << "cc " << static_cast<int>(control.controller) << ' ' << control.value;
                break;
            case ContinuousEvent::Kind::channel_pressure: out << "pressure " << control.value; break;
            case ContinuousEvent::Kind::poly_pressure:
                out << "poly " << static_cast<int>(control.controller) << ' ' << control.value;
                break;
            }
            out << '\n';
        }
    }
}

bool parse_control(const Fields& fields, ParseContext& context) {
    if (!fields.at_least(5)) return context.fail("malformed control record");
    const auto pattern = fields.integer(1);
    const auto tick = fields.integer(2);
    const auto kind = fields.text(3);
    if (!pattern || !tick || !kind || *pattern < 0 ||
        static_cast<std::size_t>(*pattern) >= context.drafts.size())
        return context.fail("control refers to a pattern that does not exist");
    ContinuousEvent event;
    event.tick = *tick;
    std::optional<long long> value;
    if (*kind == "bend" && fields.count(5)) {
        event.kind = ContinuousEvent::Kind::pitch_bend;
        value = fields.integer(4);
    } else if (*kind == "pressure" && fields.count(5)) {
        event.kind = ContinuousEvent::Kind::channel_pressure;
        value = fields.integer(4);
    } else if ((*kind == "cc" || *kind == "poly") && fields.count(6)) {
        event.kind = *kind == "cc" ? ContinuousEvent::Kind::control_change
                                   : ContinuousEvent::Kind::poly_pressure;
        const auto controller = fields.integer(4);
        if (!controller || *controller < 0 || *controller > 127)
            return context.fail("malformed control record");
        event.controller = static_cast<std::uint8_t>(*controller);
        value = fields.integer(5);
    } else {
        return context.fail("malformed control record");
    }
    if (!value || *value < 0 || *value > 16383) return context.fail("malformed control record");
    event.value = static_cast<std::uint16_t>(*value);
    const auto& draft = context.drafts[static_cast<std::size_t>(*pattern)];
    if (!Pattern::valid_continuous(event, draft.length))
        return context.fail("control lies outside its pattern or its range");
    context.continuous.emplace_back(static_cast<std::size_t>(*pattern), event);
    return true;
}

// Runs after the core module has built the patterns from their drafts.
bool finish_continuous(ParseContext& context) {
    auto& patterns = context.project.song.patterns;
    for (const auto& [index, event] : context.continuous) {
        if (index >= patterns.size()) return context.fail("control refers to a missing pattern");
        patterns[index].pattern.add_continuous(event);
    }
    return true;
}

constexpr std::array continuous_handlers{
    RecordHandler{"control", parse_control},
};

} // namespace

const RecordModule& continuous_records() {
    static const RecordModule module{"continuous", write_continuous, continuous_handlers,
                                     finish_continuous};
    return module;
}

} // namespace blokkily::project_io
