// Pattern swing. Written only for a pattern whose swing is not 0, so a
// project that never swung saves the records it did before. A file without
// the record loads with every pattern straight.
//
//   swing <pattern-index> <percent>
//
// Percent is 0 to 100. At most one record per pattern. Anything else is
// refused rather than clamped.

#include "records.hpp"

#include <array>

namespace blokkily::project_io {
namespace {

void write_swing(const Project& project, WriteContext& context) {
    for (std::size_t index = 0; index < project.song.patterns.size(); ++index) {
        const int amount = project.song.patterns[index].pattern.swing();
        if (amount == 0) continue;
        context.out << "swing " << index << ' ' << amount << '\n';
    }
}

bool parse_swing(const Fields& fields, ParseContext& context) {
    if (!fields.count(3)) return context.fail("malformed swing record");
    const auto pattern = fields.integer(1);
    const auto amount = fields.integer(2);
    if (!pattern || !amount || *pattern < 0 ||
        static_cast<std::size_t>(*pattern) >= context.drafts.size() || *amount < 0 ||
        *amount > 100)
        return context.fail("malformed swing record");
    const auto index = static_cast<std::size_t>(*pattern);
    if (context.pattern_swing.size() <= index) context.pattern_swing.resize(index + 1);
    if (context.pattern_swing[index]) return context.fail("more than one swing record for a pattern");
    context.pattern_swing[index] = static_cast<int>(*amount);
    return true;
}

bool finish_swing(ParseContext& context) {
    auto& patterns = context.project.song.patterns;
    if (context.pattern_swing.size() > patterns.size())
        return context.fail("swing record names a pattern the file does not have");
    for (std::size_t index = 0; index < context.pattern_swing.size(); ++index) {
        if (!context.pattern_swing[index]) continue;
        patterns[index].pattern.set_swing(*context.pattern_swing[index]);
    }
    context.pattern_swing.clear();
    return true;
}

constexpr std::array swing_handlers{
    RecordHandler{"swing", parse_swing},
};

} // namespace

const RecordModule& swing_records() {
    static const RecordModule module{"swing", write_swing, swing_handlers, finish_swing};
    return module;
}

} // namespace blokkily::project_io
