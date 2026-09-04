#pragma once

#include "blokkily/model/pattern.hpp"

#include <cstdint>
#include <vector>

namespace blokkily {

class Scheduler {
public:
    // Compiles one loop of the pattern into notes and the parameter changes its
    // locks demand. Both come from a single pass so that a step suppressed by
    // probability or a loop condition takes its parameter locks with it.
    [[nodiscard]] ScheduledEvents render(
        const Pattern& pattern, std::uint64_t loop_number, std::uint64_t seed) const;
    [[nodiscard]] std::vector<ScheduledNote> render_loop(
        const Pattern& pattern, std::uint64_t loop_number, std::uint64_t seed) const;

private:
    [[nodiscard]] static bool should_play(
        const Trigger& event, std::uint64_t loop_number, std::uint64_t seed);
};

} // namespace blokkily

