#pragma once

// Chunk stage 9 (plan F-D): the strip gain for one chunk. With automation
// (item 3.1) it is the envelope ramp times the solo gate; until then it is the
// static gain the mixer atomics hold.

#include "blokkily/audio/mixer.hpp"

#include <cstddef>
#include <cstdint>

namespace blokkily::engine {

// Per track: where the strip's automation envelope is.
struct StripAutomation {};

// Per arrangement: the compiled automation lanes.
struct ArrangementAutomation {};

[[nodiscard]] StripGain chunk_strip_gain(StripAutomation& automation,
                                         const ArrangementAutomation& lanes, StripGain fixed,
                                         std::uint64_t song_position, std::size_t frames,
                                         bool from_timeline) noexcept;

} // namespace blokkily::engine
