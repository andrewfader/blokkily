#include "blokkily/notation/pattern_bridge.hpp"

#include "blokkily/notation/duration.hpp"
#include "blokkily/notation/layout.hpp"
#include "blokkily/notation/spelling.hpp"

#include <algorithm>
#include <variant>

namespace blokkily::notation {

namespace {

// One sounding voice, flattened out of a trigger.
struct Voice {
    int midi;
    float velocity;
    double cents;
    Tick duration;
};

std::vector<Voice> voices_of(const Trigger& trigger) {
    std::vector<Voice> out;
    if (const auto* note = std::get_if<Note>(&trigger.musical_data)) {
        out.push_back({note->key, note->velocity, note->cents, trigger.duration});
    } else if (const auto* chord = std::get_if<Chord>(&trigger.musical_data)) {
        for (std::size_t i = 0; i < chord->intervals.size(); ++i) {
            const double cents =
                i < chord->cents.size() ? chord->cents[i] : 0.0;
            out.push_back({static_cast<int>(chord->root) + chord->intervals[i],
                           voice_velocity(*chord, i), cents,
                           voice_duration(*chord, i, trigger.duration)});
        }
    }
    return out;
}

void emit_tied(Measure& measure, const SpelledPitch& spelled, float velocity,
               double cents, const std::vector<NoteDuration>& chain, int voice) {
    for (std::size_t i = 0; i < chain.size(); ++i) {
        NotationNote note;
        note.spelled = spelled;
        note.duration = chain[i];
        note.velocity = velocity;
        note.cents = cents;
        note.voice = voice;
        note.tie_start = i + 1 < chain.size();
        note.tie_end = i > 0;
        measure.objects.emplace_back(note);
    }
}

} // namespace

Staff to_staff(const Pattern& pattern, const KeySignature& key, const Clef& clef,
               Tick ticks_per_beat) {
    Staff staff;
    staff.clef = clef;
    staff.key = key;
    Measure measure;
    for (const Trigger& trigger : pattern.events()) {
        int voice = 1;
        for (const Voice& v : voices_of(trigger)) {
            const auto chain = from_ticks(v.duration, ticks_per_beat);
            emit_tied(measure, spell(v.midi, key), v.velocity, v.cents, chain, voice++);
        }
    }
    staff.measures.push_back(std::move(measure));
    return staff;
}

Pattern to_pattern(const Staff& staff, Tick ticks_per_beat) {
    struct Pending {
        Tick start = 0;
        SpelledPitch spelled{};
        int voice = 1;
        Tick duration_ticks = 0;
        float velocity = 0.8F;
        double cents = 0.0;
        bool open = false;
    };

    const Tick measure_ticks = staff.time.metered()
        ? ticks_per_beat * 4 * staff.time.beats() / staff.time.beat_unit()
        : 0;

    // Size the pattern up front: Pattern::add rejects a trigger that starts
    // past the pattern's length.
    Tick total = 0;
    for (std::size_t m = 0; m < staff.measures.size(); ++m) {
        Tick content = 0;
        for (const StaffObject& object : staff.measures[m].objects) {
            content += std::visit(
                [&](const auto& o) { return ticks(o.duration, ticks_per_beat); }, object);
        }
        total += staff.time.metered() ? std::max(content, measure_ticks) : content;
    }
    Pattern pattern(std::max<Tick>(1, total), ticks_per_beat);

    Tick cursor = 0;
    Pending pending;
    // For polyphonic round-trip: increasing voice numbers indicate simultaneous
    // notes (a chord). Track the start of the current simultaneous group.
    Tick group_start = 0;
    int prev_voice = 0;

    const auto flush = [&](Pending& p) {
        if (!p.open) return;
        Trigger trigger;
        trigger.start = p.start;
        trigger.duration = p.duration_ticks;
        Note note;
        note.key = static_cast<std::int16_t>(to_midi(p.spelled) +
                                             staff.transpose_octaves * 12);
        note.velocity = p.velocity;
        note.cents = p.cents;
        trigger.musical_data = note;
        (void)pattern.add(std::move(trigger));
        p.open = false;
    };

    for (std::size_t m = 0; m < staff.measures.size(); ++m) {
        for (const StaffObject& object : staff.measures[m].objects) {
            if (const auto* rest = std::get_if<Rest>(&object)) {
                flush(pending);
                cursor += ticks(rest->duration, ticks_per_beat);
            } else if (const auto* note = std::get_if<NotationNote>(&object)) {
                const Tick d = ticks(note->duration, ticks_per_beat);
                // A tie continues the open chain only when it is the same
                // pitch and voice: a tie_end on another note is its own note
                // (e.g. a tie carried in from a previous system), never part
                // of this chain.
                const bool continues =
                    note->tie_end && pending.open && note->voice == pending.voice &&
                    note->spelled.step == pending.spelled.step &&
                    note->spelled.accidental == pending.spelled.accidental &&
                    note->spelled.octave == pending.spelled.octave;
                if (continues) {
                    pending.duration_ticks += d;
                    cursor += d;
                    if (!note->tie_start) flush(pending);
                } else {
                    flush(pending); // defensive: a tie chain must not overlap
                    // Increasing voice numbers = simultaneous notes (chord).
                    // Use the group start time; otherwise use the cursor.
                    const Tick start_tick =
                        (prev_voice != 0 && note->voice > prev_voice) ? group_start : cursor;
                    if (!(prev_voice != 0 && note->voice > prev_voice)) {
                        group_start = cursor;
                    }
                    if (note->tie_start) {
                        pending = {start_tick, note->spelled, note->voice, d,
                                   note->velocity, note->cents, true};
                    } else {
                        Pending single{start_tick, note->spelled, note->voice, d,
                                       note->velocity, note->cents, true};
                        flush(single);
                    }
                    if (!(prev_voice != 0 && note->voice > prev_voice)) {
                        cursor += d;
                    }
                    prev_voice = note->voice;
                }
            }
        }
        if (staff.time.metered())
            cursor = static_cast<Tick>(m + 1) * measure_ticks;
    }
    flush(pending);
    return pattern;
}

} // namespace blokkily::notation
