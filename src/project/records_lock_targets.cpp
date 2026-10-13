// Step locks on insert effects. The format 4 `lock` line keeps its shape and
// names the instrument's parameter; a lock on one of the track's insert
// effects is followed by an additive record naming which:
//
//   lockslot <pattern> <trigger-id> <lock> <slot>
//
// `lock` is the lock's place among its trigger's locks, `slot` the insert
// (0 is the first effect of the track's chain). It is written only for a
// lock on an insert, so a file with none is unchanged.

#include "records.hpp"

#include <array>

namespace blokkily::project_io {
namespace {

void write_lock_targets(const Project& project, WriteContext& context) {
    auto& out = context.out;
    for (std::size_t index = 0; index < project.song.patterns.size(); ++index)
        for (const auto& trigger : project.song.patterns[index].pattern.events())
            for (std::size_t lock = 0; lock < trigger.locks.size(); ++lock)
                if (trigger.locks[lock].slot >= 0)
                    out << "lockslot " << index << ' ' << trigger.id << ' ' << lock << ' '
                        << trigger.locks[lock].slot << '\n';
}

bool parse_lockslot(const Fields& fields, ParseContext& context) {
    const auto pattern_index = fields.integer(1);
    const auto id = fields.integer(2);
    const auto lock = fields.integer(3);
    const auto slot = fields.integer(4);
    if (!fields.count(5) || !pattern_index || !id || !lock || !slot || *pattern_index < 0 ||
        static_cast<std::size_t>(*pattern_index) >= context.drafts.size() || *lock < 0 ||
        *slot < 0 || *slot > 0x7FFFFFFF)
        return context.fail("malformed lockslot record");
    auto* trigger = context.drafts[static_cast<std::size_t>(*pattern_index)].find(
        static_cast<EventId>(*id));
    if (trigger == nullptr) return context.fail("lockslot refers to an unknown trigger");
    if (static_cast<std::size_t>(*lock) >= trigger->locks.size())
        return context.fail("lockslot refers to a lock its trigger does not have");
    trigger->locks[static_cast<std::size_t>(*lock)].slot = static_cast<std::int32_t>(*slot);
    return true;
}

constexpr std::array lock_target_handlers{RecordHandler{"lockslot", parse_lockslot}};

} // namespace

const RecordModule& lock_target_records() {
    static const RecordModule module{"lock_targets", write_lock_targets, lock_target_handlers,
                                     nullptr};
    return module;
}

} // namespace blokkily::project_io
