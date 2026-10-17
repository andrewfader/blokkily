#include "blokkily/notation/pattern_bridge.hpp"

#include "blokkily/notation/duration.hpp"
#include "blokkily/notation/spelling.hpp"

#include <algorithm>
#include <map>
#include <variant>

namespace blokkily::notation {

namespace {

// One sounding note, flattened out of a trigger.
struct Sound {
    Tick start;
    Tick duration;
    int midi;
    double cents;
    float velocity;
};

// Notes that start and end together: one chord in one voice.
struct Group {
    Tick start;
    Tick duration;
    std::vector<Sound> notes; // lowest first
};

std::vector<Sound> sounds_of(const Pattern& pattern) {
    std::vector<Sound> out;
    for (const Trigger& trigger : pattern.events()) {
        if (const auto* note = std::get_if<Note>(&trigger.musical_data)) {
            out.push_back({trigger.start, trigger.duration, note->key, note->cents, note->velocity});
        } else if (const auto* chord = std::get_if<Chord>(&trigger.musical_data)) {
            for (std::size_t i = 0; i < chord->intervals.size(); ++i)
                out.push_back({trigger.start, voice_duration(*chord, i, trigger.duration),
                               static_cast<int>(chord->root) + chord->intervals[i],
                               i < chord->cents.size() ? chord->cents[i] : 0.0,
                               voice_velocity(*chord, i)});
        }
    }
    std::erase_if(out, [](const Sound& s) { return s.duration <= 0 || s.midi < 0 || s.midi > 127; });
    std::sort(out.begin(), out.end(), [](const Sound& a, const Sound& b) {
        if (a.start != b.start) return a.start < b.start;
        if (a.duration != b.duration) return a.duration > b.duration;
        return a.midi < b.midi;
    });
    return out;
}

Tick bar_ticks(const TimeSignature& time, Tick ticks_per_beat) {
    return ticks_per_beat * 4 * time.beats() / time.beat_unit();
}

} // namespace

Tick measure_ticks(const Measure& measure, const TimeSignature& time, Tick ticks_per_beat) {
    if (time.metered() && !measure.pickup) return bar_ticks(time, ticks_per_beat);
    std::map<int, Tick> cursor;
    Tick longest = 0;
    for (const StaffObject& object : measure.objects) {
        if (const auto* rest = std::get_if<Rest>(&object)) {
            longest = std::max(longest, cursor[rest->voice] += ticks(rest->duration, ticks_per_beat));
        } else {
            const auto& note = std::get<NotationNote>(object);
            if (!note.chord)
                longest = std::max(longest, cursor[note.voice] += ticks(note.duration, ticks_per_beat));
        }
    }
    return longest;
}

std::vector<Placed> place(const Staff& staff, Tick ticks_per_beat) {
    std::vector<Placed> placed;
    TimeSignature time = staff.time;
    Tick measure_start = 0;
    for (std::size_t m = 0; m < staff.measures.size(); ++m) {
        const Measure& measure = staff.measures[m];
        if (measure.time) time = *measure.time;
        std::map<int, Tick> cursor;
        std::map<int, Tick> onset;
        for (std::size_t i = 0; i < measure.objects.size(); ++i) {
            const StaffObject& object = measure.objects[i];
            const int voice = std::visit([](const auto& o) { return o.voice; }, object);
            const Tick length = std::visit([&](const auto& o) { return ticks(o.duration, ticks_per_beat); }, object);
            Tick& at = cursor[voice];
            const auto* note = std::get_if<NotationNote>(&object);
            if (note == nullptr || !note->chord) {
                onset[voice] = at;
                at += length;
            }
            placed.push_back({m, i, measure_start + onset[voice], length, voice, false});
        }
        measure_start += measure_ticks(measure, time, ticks_per_beat);
    }
    std::stable_sort(placed.begin(), placed.end(), [](const Placed& a, const Placed& b) {
        return a.measure != b.measure ? a.measure < b.measure : a.onset < b.onset;
    });

    // The key in force in each bar.
    std::vector<KeySignature> keys;
    for (const Measure& measure : staff.measures)
        keys.push_back(measure.key ? *measure.key : keys.empty() ? staff.key : keys.back());
    std::map<std::pair<int, int>, Accidental> said;
    for (std::size_t i = 0; i < placed.size(); ++i) {
        Placed& entry = placed[i];
        if (i > 0 && placed[i - 1].measure != entry.measure) said.clear();
        const auto* note = std::get_if<NotationNote>(&staff.measures[entry.measure].objects[entry.index]);
        if (note == nullptr || !staff.clef.pitched()) continue;
        const auto where = std::make_pair(static_cast<int>(note->spelled.step), note->spelled.octave);
        const auto found = said.find(where);
        const Accidental expected =
            found != said.end() ? found->second : keys[entry.measure].accidental_for(note->spelled.step);
        entry.accidental_shown = !note->tie_end && note->spelled.accidental != expected;
        said[where] = note->spelled.accidental;
    }
    return placed;
}

Staff to_staff(const Pattern& pattern, const KeySignature& key, const Clef& clef,
               const TimeSignature& time) {
    const Tick tpb = pattern.ticks_per_beat();
    Staff staff;
    staff.clef = clef;
    staff.key = key;
    staff.time = time;

    // Notes that start and end together share a chord.
    std::vector<Group> groups;
    for (const Sound& sound : sounds_of(pattern)) {
        if (!groups.empty() && groups.back().start == sound.start &&
            groups.back().duration == sound.duration)
            groups.back().notes.push_back(sound);
        else
            groups.push_back({sound.start, sound.duration, {sound}});
    }
    // Each chord goes to the first voice that is free when it starts, so a
    // note held under moving ones keeps its own voice.
    std::vector<std::vector<const Group*>> voices(1);
    std::vector<Tick> voice_end(1, 0);
    Tick end = std::max<Tick>(pattern.length(), 1);
    for (const Group& group : groups) {
        std::size_t v = 0;
        while (v < voices.size() && voice_end[v] > group.start) ++v;
        if (v == voices.size()) {
            voices.emplace_back();
            voice_end.push_back(0);
        }
        voices[v].push_back(&group);
        voice_end[v] = group.start + group.duration;
        end = std::max(end, voice_end[v]);
    }

    const bool metered = time.metered();
    const Tick bar = metered ? bar_ticks(time, tpb) : end;
    const std::size_t measures = metered ? static_cast<std::size_t>((end + bar - 1) / bar) : 1;
    staff.measures.resize(std::max<std::size_t>(1, measures));

    for (std::size_t v = 0; v < voices.size(); ++v) {
        const int voice = static_cast<int>(v) + 1;
        std::vector<std::vector<StaffObject>> per_measure(staff.measures.size());
        std::vector<bool> sounds_in(staff.measures.size(), false);
        // A stretch of the voice, split at barlines and tied across them.
        const auto emit = [&](Tick from, Tick to, const Group* group) {
            bool first_piece = true;
            for (Tick at = from; at < to;) {
                const auto m = metered ? static_cast<std::size_t>(at / bar) : 0;
                const Tick piece_end = std::min(to, metered ? static_cast<Tick>(m + 1) * bar : to);
                const auto chain = notate_ticks(piece_end - at, tpb);
                for (std::size_t k = 0; k < chain.size(); ++k) {
                    if (group == nullptr) {
                        per_measure[m].emplace_back(Rest{chain[k], voice});
                        continue;
                    }
                    sounds_in[m] = true;
                    for (std::size_t i = 0; i < group->notes.size(); ++i) {
                        const Sound& sound = group->notes[i];
                        NotationNote note;
                        note.spelled = spell(sound.midi, key);
                        note.duration = chain[k];
                        // Chant is written in bare noteheads.
                        note.duration.stemless = !metered;
                        note.velocity = sound.velocity;
                        note.cents = sound.cents;
                        note.voice = voice;
                        note.chord = i > 0;
                        note.tie_end = !(first_piece && k == 0);
                        note.tie_start = !(piece_end == to && k + 1 == chain.size());
                        per_measure[m].emplace_back(note);
                    }
                }
                first_piece = false;
                at = piece_end;
            }
        };
        Tick cursor = 0;
        for (const Group* group : voices[v]) {
            if (group->start > cursor) emit(cursor, group->start, nullptr);
            emit(group->start, group->start + group->duration, group);
            cursor = group->start + group->duration;
        }
        // The first voice fills every bar; another fills only the bar it
        // stops in, and is left out of bars where it has nothing to play.
        const Tick fill_to = v == 0 ? static_cast<Tick>(staff.measures.size()) * bar
                                    : (cursor + bar - 1) / bar * bar;
        if (cursor < fill_to) emit(cursor, fill_to, nullptr);
        for (std::size_t m = 0; m < staff.measures.size(); ++m) {
            if (v > 0 && !sounds_in[m]) continue;
            auto& objects = staff.measures[m].objects;
            objects.insert(objects.end(), per_measure[m].begin(), per_measure[m].end());
        }
    }
    return staff;
}

Pattern to_pattern(const Staff& staff, Tick ticks_per_beat, int* dropped) {
    struct Placed {
        Tick start;
        Tick duration;
        int midi;
        double cents;
        float velocity;
    };
    // A tie that has started and not yet stopped: the note it extends, and
    // where the tied note must begin to continue it.
    struct OpenTie {
        std::size_t placed;
        Tick next;
    };
    std::vector<Placed> placed;
    std::vector<OpenTie> open;
    int left_out = 0;
    TimeSignature time = staff.time;
    Tick measure_start = 0;
    Tick last_end = 0;
    for (const Measure& measure : staff.measures) {
        if (measure.time) time = *measure.time;
        std::map<int, Tick> cursor;
        std::map<int, Tick> onset;
        for (const StaffObject& object : measure.objects) {
            if (const auto* rest = std::get_if<Rest>(&object)) {
                cursor[rest->voice] += ticks(rest->duration, ticks_per_beat);
                continue;
            }
            const auto& note = std::get<NotationNote>(object);
            const Tick length = ticks(note.duration, ticks_per_beat);
            Tick& at = cursor[note.voice];
            if (!note.chord) {
                onset[note.voice] = at;
                at += length;
            }
            const Tick start = measure_start + onset[note.voice];
            const int midi = to_midi(note.spelled) + staff.transposition();
            if (note.tie_end) {
                const auto continued = std::find_if(open.begin(), open.end(), [&](const OpenTie& tie) {
                    const Placed& from = placed[tie.placed];
                    return tie.next == start && from.midi == midi && from.cents == note.cents;
                });
                if (continued != open.end()) {
                    placed[continued->placed].duration += length;
                    continued->next += length;
                    last_end = std::max(last_end, continued->next);
                    if (!note.tie_start) open.erase(continued);
                    continue;
                }
            }
            if (midi < 0 || midi > 127) {
                ++left_out;
                continue;
            }
            placed.push_back({start, length, midi, note.cents, note.velocity});
            last_end = std::max(last_end, start + length);
            if (note.tie_start) open.push_back({placed.size() - 1, start + length});
        }
        measure_start += measure_ticks(measure, time, ticks_per_beat);
    }
    if (dropped != nullptr) *dropped = left_out;

    Pattern pattern(std::max<Tick>({1, measure_start, last_end}), ticks_per_beat);
    std::stable_sort(placed.begin(), placed.end(),
                     [](const Placed& a, const Placed& b) { return a.start < b.start; });
    for (const Placed& p : placed) {
        Trigger trigger;
        trigger.start = p.start;
        trigger.duration = p.duration;
        trigger.musical_data = Note{static_cast<std::int16_t>(p.midi), p.velocity, 0.0F, p.cents};
        (void)pattern.add(std::move(trigger));
    }
    return pattern;
}

} // namespace blokkily::notation
