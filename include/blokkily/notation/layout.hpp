#pragma once

// Staff geometry in diatonic steps: where a spelled pitch sits on a clef's
// staff, and whether it needs an accidental drawn in front of it.

#include "blokkily/notation/clef.hpp"
#include "blokkily/notation/pitch.hpp"
#include "blokkily/notation/signature.hpp"

namespace blokkily::notation {

// Diatonic steps above the clef's bottom line. The bottom line itself is 0,
// the space above it is 1, the next line is 2; negative positions hang below
// the staff on ledger lines. Transposition (Staff::transpose_octaves) is NOT
// applied here — the caller shifts the pitch first if it wants the sounding
// position rather than the written one.
[[nodiscard]] inline int staff_position(const SpelledPitch& pitch, const Clef& clef) noexcept {
    return diatonic_index(pitch) - clef.bottom_line_diatonic();
}

// True when the note needs an accidental printed: its accidental differs from
// what the key signature already imposes on its step. (Courtesy and
// barline-scoped cancellation rules are a view concern, not modelled here.)
[[nodiscard]] inline bool needs_accidental(const SpelledPitch& pitch,
                                          const KeySignature& key) noexcept {
    return pitch.accidental != key.accidental_for(pitch.step);
}

// How many ledger lines a position needs: 0 while on the staff (positions
// 0..8) or in the space just beyond it, then one per two steps further out.
[[nodiscard]] inline int ledger_lines(int position) noexcept {
    if (position >= 0 && position <= 8) return 0;
    return position > 8 ? (position - 8) / 2 : (-position) / 2;
}

} // namespace blokkily::notation
