// Per-note expression (phase 2, wave 4.1, MPE): how each voice of a step
// moved while it sounded - its pitch, timbre and pressure - one additive
// record per value, after the triggers they belong to:
//
//   express <pattern> <trigger-id> <voice> <offset> pitch <semitones -96..96>
//   express <pattern> <trigger-id> <voice> <offset> timbre <0..1>
//   express <pattern> <trigger-id> <voice> <offset> pressure <0..1>
//
// Format 5's records are additive (records.hpp): a file without them (every
// file written before MPE) loads with no expression and plays as it did; a
// reader from before them refuses a file that holds one ("unknown record")
// rather than play its notes without their expression. They are written in
// the trigger's own order, so a save is byte-stable.

#include "records.hpp"

#include <array>
#include <cmath>

namespace blokkily::project_io {
namespace {

const char* kind_name(NoteExpression::Kind kind) {
    switch (kind) {
    case NoteExpression::Kind::pitch: return "pitch";
    case NoteExpression::Kind::timbre: return "timbre";
    case NoteExpression::Kind::pressure: return "pressure";
    }
    return "pitch";
}

void write_expression(const Project& project, WriteContext& context) {
    auto& out = context.out;
    for (std::size_t index = 0; index < project.song.patterns.size(); ++index)
        for (const auto& trigger : project.song.patterns[index].pattern.events())
            for (const auto& point : trigger.expression)
                out << "express " << index << ' ' << trigger.id << ' '
                    << static_cast<int>(point.voice) << ' ' << point.offset << ' '
                    << kind_name(point.kind) << ' ' << number(point.value) << '\n';
}

bool parse_express(const Fields& fields, ParseContext& context) {
    if (!fields.count(7)) return context.fail("malformed express record");
    const auto pattern = fields.integer(1);
    const auto id = fields.integer(2);
    const auto voice = fields.integer(3);
    const auto offset = fields.integer(4);
    const auto kind = fields.text(5);
    const auto value = fields.real(6);
    if (!pattern || !id || !voice || !offset || !kind || !value || *pattern < 0 ||
        static_cast<std::size_t>(*pattern) >= context.drafts.size() || *voice < 0 ||
        *voice > 255 || *offset < 0)
        return context.fail("malformed express record");
    NoteExpression point;
    point.voice = static_cast<std::uint8_t>(*voice);
    point.offset = *offset;
    if (*kind == "pitch") point.kind = NoteExpression::Kind::pitch;
    else if (*kind == "timbre") point.kind = NoteExpression::Kind::timbre;
    else if (*kind == "pressure") point.kind = NoteExpression::Kind::pressure;
    else return context.fail("malformed express record");
    point.value = static_cast<float>(*value);
    if (!std::isfinite(*value) || !NoteExpression::valid_value(point.kind, point.value))
        return context.fail("note expression value is out of range");
    auto* trigger = context.drafts[static_cast<std::size_t>(*pattern)].find(
        static_cast<EventId>(*id));
    if (trigger == nullptr) return context.fail("express refers to an unknown trigger");
    // Whether the voice exists is checked with the whole trigger, when the
    // pattern is built (Pattern::restore).
    trigger->expression.push_back(point);
    return true;
}

constexpr std::array expression_handlers{
    RecordHandler{"express", parse_express},
};

} // namespace

const RecordModule& expression_records() {
    static const RecordModule module{"expression", write_expression, expression_handlers,
                                     nullptr};
    return module;
}

} // namespace blokkily::project_io
