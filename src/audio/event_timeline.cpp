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
    return compile_timeline(scheduled, TickClock::uniform(samples_per_tick, 0.0), last_sample);
}

std::vector<TimedPluginEvent> compile_timeline(
    const ScheduledEvents& scheduled, const TickClock& clock, std::uint64_t last_sample) {
    std::vector<TimedPluginEvent> timeline;
    timeline.reserve(scheduled.parameters.size() + scheduled.continuous.size() +
                     scheduled.expressions.size() + scheduled.notes.size() * 2);
    const auto at = [&clock, last_sample](Tick tick) {
        return std::min(sample_for_tick(clock, static_cast<double>(tick)), last_sample);
    };
    for (const auto& parameter : scheduled.parameters) {
        const auto type = parameter.kind == ParameterLock::Kind::modulation
                              ? PluginEvent::Type::parameter_modulation
                              : PluginEvent::Type::parameter_value;
        timeline.push_back({at(parameter.start), 0,
                            {type, 0, parameter.index, parameter.value}});
    }
    // Controller movements (wave 4.1) go before a note on the same sample,
    // as locks do: a note struck with the wheel already bent sounds bent.
    for (const auto& control : scheduled.continuous)
        timeline.push_back({at(control.start), 0,
                            {PluginEvent::Type::midi_raw, 0,
                             static_cast<std::int32_t>(midi_raw_of(control.event)), 0.0}});
    for (const auto& note : scheduled.notes) {
        timeline.push_back({at(note.start), 1,
                            {PluginEvent::Type::note_on, 0, note.key, note.velocity,
                             note.cents, static_cast<std::uint8_t>(note.expressive ? 1 : 0)}});
        timeline.push_back({at(note.start + note.duration), 1,
                            {PluginEvent::Type::note_off, 0, note.key, 0.0, note.cents}});
    }
    // Per-note expression after the note-on on its sample: it moves a note
    // that has been struck.
    for (const auto& expression : scheduled.expressions)
        timeline.push_back({at(expression.start), 2,
                            {PluginEvent::Type::note_expression, 0, expression.key,
                             static_cast<double>(expression.value), expression.cents,
                             static_cast<std::uint8_t>(expression.kind)}});
    std::stable_sort(timeline.begin(), timeline.end(),
        [](const TimedPluginEvent& a, const TimedPluginEvent& b) {
            return a.sample < b.sample || (a.sample == b.sample && a.rank < b.rank);
        });
    return timeline;
}

} // namespace blokkily
