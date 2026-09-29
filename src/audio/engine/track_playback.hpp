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

// The controllers a timeline can move, as chase keys: CC 0..127, then the
// pitch wheel, then channel pressure.
inline constexpr std::size_t controller_keys = 130;
[[nodiscard]] inline std::size_t controller_key(std::uint32_t raw) noexcept {
    switch (raw & 0xF0U) {
    case 0xE0U: return 128;
    case 0xD0U: return 129;
    case 0xB0U: return (raw >> 8) & 0x7FU;
    default: return controller_keys;
    }
}

// Events played from the interface, waiting for the next block.
using LiveEvents = SpscQueue<PluginEvent, 128>;

// The most events one track can receive in a single chunk: the arrangement's
// releases, live, routed and performed input, and the timeline's budget. The scratch that
// holds them is sized once, in prepare(), so raising a budget costs memory
// there and never a stack frame in the callback.
inline constexpr std::size_t maximum_events_per_chunk =
    timeline_event_budget + 128 + controller_keys + LiveEvents::capacity() +
    InputQueue::capacity() + PerformQueue::capacity() + automation_event_budget;

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
    // Controller chase (wave 4.1): set when the playhead jumps (a seek, a
    // wrap, the transport starting), and served by the next chunk, which
    // first plays every controller the timeline moves at the value it had
    // reached there. `chase_values` is its scratch, one per controller key.
    bool chase = false;
    std::array<std::int32_t, controller_keys> chase_values{};
    // The chunk (SongEngine's chunk serial) whose instrument output the
    // source's instrument already wrote into this track's buffer: a track fed
    // by an instrument output keeps it rather than starting silent. Callback
    // only.
    std::uint64_t fed_chunk = 0;
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
