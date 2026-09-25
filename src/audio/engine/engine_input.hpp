#pragma once

// Chunk stage 6 (plan F-D): audio-input monitoring, and the point where a
// capture taps the raw input before anything else is applied. Item 3.2 fills
// this in; until then it is empty and adds nothing.

#include "blokkily/plugins/plugin.hpp"

#include <cstdint>

namespace blokkily::engine {

// Per track: which device input the track monitors and records.
struct InputPlayback {};

void add_input_monitoring(InputPlayback& input, StereoBlock track,
                          std::uint64_t song_position, bool from_timeline) noexcept;

} // namespace blokkily::engine
