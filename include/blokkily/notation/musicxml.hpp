#pragma once

// MusicXML import/export for blokkily::notation scores.
//
// What is written and read (see docs/notation-design.md):
//   - <score-partwise> 4.0: the part list, and per part its measures with
//     <attributes> (divisions, key, time or senza-misura, staves, clef,
//     transpose) in the first measure and wherever a measure changes them,
//     pickups as implicit measures.
//   - Notes and rests voice by voice, rewound with <backup>; chords with
//     <chord/>; ties, dots, tuplets (<time-modification>), stemless chant
//     heads, lyrics (syllabic and extend), velocity as the note's dynamics,
//     and a retune as a fractional <alter>.
//   - Import takes a note's time from its <duration> and turns <forward>
//     gaps into rests. Grace and cue notes are skipped; unknown elements
//     are ignored.
//   - parse() throws std::runtime_error on malformed XML, a non-partwise
//     root, or values the model cannot hold (a staff number outside 1-16,
//     a non-positive <divisions>, an unsupported meter).
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
