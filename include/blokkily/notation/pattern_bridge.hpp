#pragma once

// The bridge between the canonical pattern model and staff notation. The
// pattern stays the single source of truth (repo invariant: the tracker, piano
// roll, step grid — and now the score view — are projections of one pattern).
//
// What notation cannot say is dropped and documented: micro-timing that is
// not on the 128th grid, probability, ratchets, loop conditions, parameter
// locks and chord strum. A chord comes back as one trigger per note at the
// same tick: the sounding pitches, starts and lengths round-trip, the chord
// object does not.

#include "blokkily/model/pattern.hpp"
#include "blokkily/notation/clef.hpp"
#include "blokkily/notation/signature.hpp"
#include "blokkily/notation/staff.hpp"

#include <vector>

namespace blokkily::notation {

// The pattern as notation, under the given clef, key and meter, at the
// pattern's own resolution. Notes are placed where they start: gaps become
// rests, notes that cross a barline are tied over it, notes that start and
// end together are one chord, and a note that overlaps a held one goes to
// another voice. A metered staff has as many measures as the pattern needs;
// an unmetered one is a single measure with no barlines.
[[nodiscard]] Staff to_staff(const Pattern& pattern, const KeySignature& key, const Clef& clef,
                             const TimeSignature& time);

// A change of meter at a tick of the pattern, as a song's meter map has them.
struct MeterChange {
    Tick at = 0;
    TimeSignature time{4, 4};
};
// As above, under meters that change: the first holds from the start, and
// each later one from the first barline at or after its tick, where the
// measure records the change.
[[nodiscard]] Staff to_staff(const Pattern& pattern, const KeySignature& key, const Clef& clef,
                             const std::vector<MeterChange>& meters);

// The staff played back into a pattern at `ticks_per_beat`: each voice of
// each measure in time, chords together, ties joined into one note. A
// metered measure lasts its meter's bar unless it is a pickup; an unmetered
// one lasts as long as its longest voice. Each note sounds under the
// transposition in force in its measure. Notes whose sounding pitch falls
// outside MIDI 0-127 are left out and counted in `dropped`.
[[nodiscard]] Pattern to_pattern(const Staff& staff, Tick ticks_per_beat, int* dropped = nullptr);

// Where each object of a staff falls: its measure and index there, its onset
// in ticks from the staff's start, its length, its voice, and - for a note -
// whether its accidental is printed. In time order (by measure, then onset;
// objects that start together keep their written order).
struct Placed {
    std::size_t measure = 0;
    std::size_t index = 0;
    Tick onset = 0;
    Tick length = 0;
    int voice = 1;
    // An accidental holds to the end of its bar: a note prints one when its
    // own differs from what its bar, or else the key, last said for its line
    // and octave. The tied end of a note never prints one, nor does a note
    // in a bar under a clef that is not pitched.
    bool accidental_shown = false;
};
[[nodiscard]] std::vector<Placed> place(const Staff& staff, Tick ticks_per_beat);

// How long a measure lasts in ticks under the meter in force there.
[[nodiscard]] Tick measure_ticks(const Measure& measure, const TimeSignature& time,
                                 Tick ticks_per_beat);

} // namespace blokkily::notation
