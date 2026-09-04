#pragma once

#include "blokkily/model/event.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <cstdint>
#include <vector>

namespace blokkily {

// A plugin event placed on a sample timeline. `rank` orders events that land on
// the same sample: parameter locks (0) before notes (1), so a step sounds with
// the value it asked for rather than the previous one.
struct TimedPluginEvent {
    std::uint64_t sample = 0;
    int rank = 0;
    PluginEvent event{};
};

// Compiles scheduled musical events onto a sample timeline, sorted ready for
// playback. `last_sample` clamps note-offs and locks that would otherwise land
// past the end of the region being played. Pattern playback and song playback
// share this so the two can never disagree about when a step sounds.
[[nodiscard]] std::vector<TimedPluginEvent> compile_timeline(
    const ScheduledEvents& scheduled, double samples_per_tick, std::uint64_t last_sample);

} // namespace blokkily
