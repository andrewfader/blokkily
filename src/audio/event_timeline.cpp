#include "blokkily/audio/event_timeline.hpp"

#include <algorithm>
#include <cmath>

namespace blokkily {

bool timeline_density_supported(std::span<const TimedPluginEvent> timeline) {
    std::size_t count = 0;
    std::uint64_t previous = 0;
    for (const auto& event : timeline) {
        count = count != 0 && event.sample == previous ? count + 1 : 1;
        if (count > timeline_event_budget) return false;
        previous = event.sample;
    }
    return true;
}

std::size_t timeline_window(std::span<const TimedPluginEvent> timeline,
                            std::uint64_t position, std::size_t frames) noexcept {
    const auto first = std::lower_bound(timeline.begin(), timeline.end(), position,
        [](const TimedPluginEvent& event, std::uint64_t sample) { return event.sample < sample; });
    if (static_cast<std::size_t>(timeline.end() - first) <= timeline_event_budget) return frames;
    const auto boundary = (first + timeline_event_budget)->sample;
    return static_cast<std::size_t>(std::min<std::uint64_t>(frames, boundary - position));
}

std::vector<TimedPluginEvent> compile_timeline(
    const ScheduledEvents& scheduled, double samples_per_tick, std::uint64_t last_sample) {
    std::vector<TimedPluginEvent> timeline;
    timeline.reserve(scheduled.parameters.size() + scheduled.notes.size() * 2);
    const auto at = [samples_per_tick, last_sample](Tick tick) {
        const auto sample = static_cast<std::uint64_t>(
            static_cast<double>(std::max<Tick>(0, tick)) * samples_per_tick);
        return std::min(sample, last_sample);
    };
    for (const auto& parameter : scheduled.parameters) {
        const auto type = parameter.kind == ParameterLock::Kind::modulation
                              ? PluginEvent::Type::parameter_modulation
                              : PluginEvent::Type::parameter_value;
        timeline.push_back({at(parameter.start), 0,
                            {type, 0, parameter.index, parameter.value}});
    }
    for (const auto& note : scheduled.notes) {
        timeline.push_back({at(note.start), 1,
                            {PluginEvent::Type::note_on, 0, note.key, note.velocity,
                             note.cents}});
        timeline.push_back({at(note.start + note.duration), 1,
                            {PluginEvent::Type::note_off, 0, note.key, 0.0, note.cents}});
    }
    std::stable_sort(timeline.begin(), timeline.end(),
        [](const TimedPluginEvent& a, const TimedPluginEvent& b) {
            return a.sample < b.sample || (a.sample == b.sample && a.rank < b.rank);
        });
    return timeline;
}

} // namespace blokkily
