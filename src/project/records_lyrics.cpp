// The syllable sung on each step (event.hpp, Trigger::lyric). Written only
// for steps that have one, after the patterns, so a song without lyrics
// saves the records it did before and a file without them loads without.
//
//   lyric <pattern-index> <trigger-id> <syllable>
//
// The syllable is escaped (any script, spaces included) and at most 256
// bytes (a longer syllable is written cut to fit, notation::fit_lyric); at
// most one record per step. Anything else is refused.

#include "records.hpp"

#include "blokkily/notation/lyrics.hpp"

#include <array>

namespace blokkily::project_io {
namespace {

void write_lyrics(const Project& project, WriteContext& context) {
    const auto& patterns = project.song.patterns;
    for (std::size_t index = 0; index < patterns.size(); ++index)
        for (const auto& trigger : patterns[index].pattern.events())
            if (!trigger.lyric.empty())
                context.out << "lyric " << index << ' ' << trigger.id << ' ' << escape(notation::fit_lyric(trigger.lyric)) << '\n';
}

bool parse_lyric(const Fields& fields, ParseContext& context) {
    const auto pattern = fields.integer(1);
    const auto id = fields.integer(2);
    const auto text = fields.text(3);
    if (!fields.count(4) || !pattern || !id || !text || *pattern < 0 ||
        static_cast<std::size_t>(*pattern) >= context.drafts.size() || text->empty() || text->size() > 256)
        return context.fail("malformed lyric record");
    auto* trigger = context.drafts[static_cast<std::size_t>(*pattern)].find(static_cast<EventId>(*id));
    if (trigger == nullptr) return context.fail("lyric record names a step that does not exist");
    if (!trigger->lyric.empty()) return context.fail("more than one lyric record for a step");
    trigger->lyric = *text;
    return true;
}

constexpr std::array lyric_handlers{
    RecordHandler{"lyric", parse_lyric},
};

} // namespace

const RecordModule& lyric_records() {
    static const RecordModule module{"lyrics", write_lyrics, lyric_handlers, nullptr};
    return module;
}

} // namespace blokkily::project_io
