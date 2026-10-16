#pragma once

// The bridge between the canonical pattern model and staff notation. The
// pattern stays the single source of truth (repo invariant: the tracker, piano
// roll, step grid — and now the score view — are projections of one pattern).
//
// to_staff renders a pattern's notes as spelled, tied notation notes: chords
// become one note per voice at the same tick, durations become tied chains of
// values. to_pattern replays a staff back into triggers. What notation cannot
// say is dropped and documented: micro-timing offsets, probability, ratchets,
// loop conditions, parameter locks, chord strum, and per-voice chord grouping
// (voices come back as separate triggers at the same tick — the sounding
// pitches, starts and lengths round-trip, the chord object does not).

#include "blokkily/model/pattern.hpp"
#include "blokkily/notation/clef.hpp"
#include "blokkily/notation/signature.hpp"
#include "blokkily/notation/staff.hpp"

namespace blokkily::notation {

// One measure holding every note of the pattern in time order, under the
// given clef and key. The staff's time signature defaults to 4/4; rests are
// never invented, so a sparse pattern yields a sparse measure.
[[nodiscard]] Staff to_staff(const Pattern& pattern, const KeySignature& key,
                            const Clef& clef, Tick ticks_per_beat);

// Replays the staff's measures in order into triggers. Metered measures each
// occupy beats*4/beat_unit beats of ticks; unmetered measures pack notes
// end to end. The staff's transpose_octaves shifts the sounding pitch.
[[nodiscard]] Pattern to_pattern(const Staff& staff, Tick ticks_per_beat);

} // namespace blokkily::notation
