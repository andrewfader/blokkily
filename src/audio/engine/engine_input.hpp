#pragma once

// Chunk stage 6 (plan F-D): audio-input monitoring, and the point where a
// capture taps the raw input before anything else is applied. Item 3.2 fills
// this in; until then it is empty and adds nothing. Gathering played input
// for the tracks it is routed to (item 2.5) lives here as well.

#include "blokkily/audio/event_queue.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace blokkily::engine {

// Per track: which device input the track monitors and records.
struct InputPlayback {};

// Chunk stage 1, input part (item 2.5): takes what a MIDI port and the
// on-screen surfaces delivered since the last block into `into`, port first,
// and returns how many. Each event names one track; a key played into several
// armed tracks arrives as one event per track. What does not fit waits in its
// queue for the next block.
std::size_t gather_input(InputQueue* port, PerformQueue& performed,
                         std::span<RoutedEvent> into) noexcept;

void add_input_monitoring(InputPlayback& input, StereoBlock track,
                          std::uint64_t song_position, bool from_timeline) noexcept;

} // namespace blokkily::engine
