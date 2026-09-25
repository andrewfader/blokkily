#pragma once

#include "blokkily/model/event.hpp"
#include "blokkily/model/timebase.hpp"
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
// playback. Each tick lands on the sample the clock places it at, rounded
// down. `last_sample` clamps note-offs and locks that would otherwise land
// past the end of the region being played. Pattern playback and song playback
// share this so the two can never disagree about when a step sounds.
[[nodiscard]] std::vector<TimedPluginEvent> compile_timeline(
    const ScheduledEvents& scheduled, const TickClock& clock, std::uint64_t last_sample);
// The same at a constant number of samples per tick, for pattern preview.
[[nodiscard]] std::vector<TimedPluginEvent> compile_timeline(
    const ScheduledEvents& scheduled, double samples_per_tick, std::uint64_t last_sample);

// Bound plugin work without discarding events. A render window ends before
// the next group that would exceed this budget; simultaneous groups must fit.
inline constexpr std::size_t timeline_event_budget = 256;
[[nodiscard]] bool timeline_density_supported(std::span<const TimedPluginEvent> timeline);
[[nodiscard]] std::size_t timeline_window(std::span<const TimedPluginEvent> timeline,
                                        std::uint64_t position, std::size_t frames) noexcept;

} // namespace blokkily
