#pragma once

// Everything the render callback holds for one track (private to SongEngine).
// Each later feature owns one member struct and the stage that reads it, so
// features touch disjoint code (plan F-D).

#include "engine_automation.hpp"
#include "engine_clips.hpp"
#include "engine_effects.hpp"
#include "engine_input.hpp"

#include "blokkily/audio/event_queue.hpp"
#include "blokkily/audio/event_timeline.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace blokkily::engine {

// Events played from the interface, waiting for the next block.
using LiveEvents = SpscQueue<PluginEvent, 128>;

// The most events one track can receive in a single chunk: the arrangement's
// releases, live, routed and performed input, and the timeline's budget. The scratch that
// holds them is sized once, in prepare(), so raising a budget costs memory
// there and never a stack frame in the callback.
inline constexpr std::size_t maximum_events_per_chunk =
    timeline_event_budget + 128 + LiveEvents::capacity() + InputQueue::capacity() +
    PerformQueue::capacity();

struct TrackPlayback {
    std::unique_ptr<PluginInstance> instrument;
    std::vector<float> left;
    std::vector<float> right;
    // The events handed to the instrument this chunk. Preallocated.
    std::vector<PluginEvent> events;
    std::size_t cursor = 0;
    // How many note-ons the arrangement has sent for each key without a
    // note-off. Read and written by the render callback alone. A note whose
    // step is erased mid-flight has no note-off left in the timeline, so
    // without this it would ring for ever.
    std::array<std::uint8_t, 128> sounding{};
    std::atomic<float> gain_left{1.0F};
    std::atomic<float> gain_right{1.0F};
    std::atomic<float> peak{0.0F};
    LiveEvents live;

    ClipPlayback clips;
    InsertChain chain;
    InputPlayback input;
    StripAutomation automation;
};

} // namespace blokkily::engine
