#include "blokkily/sequencer/scheduler.hpp"

#include <algorithm>
#include <cmath>

namespace blokkily {
namespace {

std::uint64_t mix(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

// `length` is how long this voice sounds: the trigger's duration for a note,
// the voice's own for a chord whose voices were held for different times.
void emit_note(std::vector<ScheduledNote>& output, const Trigger& event,
               const Note& note, Tick base_start, Tick length) {
    const auto ratchets = static_cast<Tick>(event.ratchets);
    const Tick slice = std::max<Tick>(1, length / ratchets);
    for (Tick ratchet = 0; ratchet < ratchets; ++ratchet) {
        output.push_back({event.id, base_start + ratchet * slice, slice,
                          note.key, note.velocity, note.cents});
    }
}

} // namespace

bool Scheduler::should_play(const Trigger& event, std::uint64_t loop_number,
                            std::uint64_t seed) {
    if (event.play_on_loop != 0 && ((loop_number - 1) % event.play_on_loop) != 0) {
        return false;
    }
    if (event.probability >= 1.0F) return true;
    if (event.probability <= 0.0F) return false;
    const auto random = mix(seed ^ mix(event.id) ^ mix(loop_number));
    const double unit = static_cast<double>(random >> 11U) * 0x1.0p-53;
    return unit < event.probability;
}

ScheduledEvents Scheduler::render(
    const Pattern& pattern, std::uint64_t loop_number, std::uint64_t seed) const {
    ScheduledEvents result;
    auto& output = result.notes;
    for (const auto& event : pattern.events()) {
        if (!should_play(event, loop_number, seed)) continue;
        const Tick start = event.start + event.micro_offset;
        // Locks land on the step they belong to, so the value is in place
        // before the note it shapes sounds.
        for (const auto& lock : event.locks)
            result.parameters.push_back({event.id, start, lock.parameter_index,
                                         lock.value, lock.kind});
        if (const auto* note = std::get_if<Note>(&event.musical_data)) {
            emit_note(output, event, *note, start, event.duration);
            continue;
        }

        const auto& chord = std::get<Chord>(event.musical_data);
        if (chord.intervals.empty()) continue;
        const auto size = static_cast<std::int32_t>(chord.intervals.size());
        for (std::int32_t voice = 0; voice < size; ++voice) {
            const auto source = (voice + chord.inversion % size + size) % size;
            const auto octave = (voice + chord.inversion) >= size ? 12 : 0;
            const double retune = static_cast<std::size_t>(source) < chord.cents.size()
                                      ? chord.cents[static_cast<std::size_t>(source)]
                                      : 0.0;
            // Each voice sounds as hard and as long as it was played; a chord
            // with neither said sounds every voice at the chord's velocity for
            // the trigger's length, as chords always have.
            const auto interval = static_cast<std::size_t>(source);
            Note note{static_cast<std::int16_t>(chord.root + chord.intervals[source] + octave),
                      voice_velocity(chord, interval), 0.0F, retune};
            emit_note(output, event, note, start + voice * chord.strum,
                      voice_duration(chord, interval, event.duration));
        }
    }
    std::stable_sort(output.begin(), output.end(), [](const auto& a, const auto& b) {
        return a.start < b.start;
    });
    std::stable_sort(result.parameters.begin(), result.parameters.end(),
        [](const auto& a, const auto& b) { return a.start < b.start; });
    return result;
}

std::vector<ScheduledNote> Scheduler::render_loop(
    const Pattern& pattern, std::uint64_t loop_number, std::uint64_t seed) const {
    return render(pattern, loop_number, seed).notes;
}

} // namespace blokkily

