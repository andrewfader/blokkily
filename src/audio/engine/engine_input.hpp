#pragma once

// Chunk stage 6 (plan F-D): audio-input monitoring, and the point where a
// capture taps the raw input before anything else is applied (item 3.2).
// Gathering played input for the tracks it is routed to (item 2.5) lives here
// as well.

#include "blokkily/audio/audio_input.hpp"
#include "blokkily/audio/audio_source.hpp"
#include "blokkily/audio/event_queue.hpp"
#include "blokkily/audio/sample_ring.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace blokkily::engine {

// Per track: which device inputs the track monitors and records. One word, so
// the callback never reads half of a route the control thread is changing.
struct InputPlayback {
    std::atomic<std::uint32_t> route{0};

    static std::uint32_t pack(const AudioInputRoute& route) noexcept;
    static AudioInputRoute unpack(std::uint32_t word) noexcept;
};

// Block stage, input part (item 2.5): takes what a MIDI port and the
// on-screen surfaces delivered since the last block into `into`, and returns
// how many. Each event names one track; a key played into several armed
// tracks arrives as one event per track. What does not fit waits in its
// queue for the next block.
//
// Each is placed in this block of `frames` where it arrived in the period
// before it, [`period_begin`, `period_end`) on the input clock: its event's
// sample_offset is that sample, so the block plays it with the timing it was
// played with, one period late. An unstamped event, or one from before the
// period, is placed on sample 0. `into` comes back in time order, events
// placed on one sample in the order they arrived.
std::size_t gather_input(InputQueue* port, PerformQueue& performed,
                         std::span<RoutedEvent> into, std::uint64_t period_begin,
                         std::uint64_t period_end, std::size_t frames) noexcept;

// 6. The track's raw device input for this chunk. With `capture` (a ring is
// connected, the song is playing from the timeline and recording) and the
// track recording, it is pushed into `capture` stamped with `song_position`,
// before anything else touches it. Monitored, it is added to the track's
// buffer: a mono input to both sides. A route naming inputs the device does
// not have hears nothing.
void add_input_monitoring(InputPlayback& input, StereoBlock track, const InputBlock& device,
                          SampleRing* capture, std::uint32_t track_index,
                          std::uint64_t song_position) noexcept;

} // namespace blokkily::engine
