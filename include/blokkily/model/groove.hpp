#pragma once

// Quantize and humanize (features/quantize_humanize.feature): edits of a
// pattern's timing and dynamics, as a whole. A trigger sits on a step (its
// start) and is played `micro_offset` ticks off it; both edits work on where
// it sounds, start + micro_offset, and write it back the same way: the
// nearest step, and the remainder as micro-timing.

#include "blokkily/model/pattern.hpp"

#include <cstdint>

namespace blokkily {

struct GrooveResult {
    int changed = 0;
    // Triggers a quantize left where they were, because the step they would
    // land on already holds another.
    int kept = 0;
};

// Moves every trigger `strength` (0..1) of the way to the nearest multiple of
// `grid` ticks. Strength 1 lands it exactly; 0.5 halves how far off it is. A
// trigger that would land on the pattern's end goes to its start, as a note
// played just before a downbeat belongs to it.
GrooveResult quantize_pattern(Pattern& pattern, Tick grid, double strength, Tick ticks_per_step);

// Moves every trigger up to `timing` ticks either way and every velocity up
// to `velocity` either way, from a generator seeded with `seed`: the same
// seed gives the same result. Chords move their voices' velocities together.
GrooveResult humanize_pattern(Pattern& pattern, Tick timing, float velocity, std::uint32_t seed,
                              Tick ticks_per_step);

} // namespace blokkily
