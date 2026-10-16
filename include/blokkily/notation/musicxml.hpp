#pragma once

// MusicXML import/export for blokkily::notation scores.
//
// v1 subset (see docs/notation-design.md):
//   - Export writes <score-partwise> 4.0: part list, per-measure <attributes>
//     (divisions, key, time or senza-misura, clef, transpose), notes with
//     pitch/unpitched, duration, voice, type, dots, ties, lyrics and stem,
//     rests, and a closing barline per part. One <part> per Score part;
//     multi-staff parts use <staves>, <staff> and <backup>.
//   - Import reads the same subset back. Unknown elements are skipped, never
//     fatal. Chords (<chord/>) import as sequential notes, grace notes are
//     skipped, tuplets are not represented: all documented v1 limits.
//   - parse() throws std::runtime_error on malformed XML or a non-partwise
//     root element.
//
// No Qt here; errors are reported by exception, matching the core library.

#include "blokkily/notation/staff.hpp"

#include <string>

namespace blokkily::notation {

// Serialises a score to a MusicXML 4.0 partwise document (UTF-8).
[[nodiscard]] std::string write(const Score& score);

// Parses a MusicXML partwise document back into a score. Unknown elements
// are ignored; malformed XML throws std::runtime_error.
[[nodiscard]] Score parse(const std::string& xml);

} // namespace blokkily::notation
