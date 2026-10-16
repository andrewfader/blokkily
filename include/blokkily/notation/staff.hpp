#pragma once

// The score hierarchy: measures hold staff objects in time order, staves hold
// measures under one clef/key/meter, parts group staves (a piano part has two),
// and a score groups parts under a title.

#include "blokkily/notation/clef.hpp"
#include "blokkily/notation/note.hpp"
#include "blokkily/notation/signature.hpp"

#include <string>
#include <vector>

namespace blokkily::notation {

struct Measure {
    std::vector<StaffObject> objects{}; // time order; rests included as written
};

struct Staff {
    Clef clef{ClefKind::Treble};
    KeySignature key{};
    TimeSignature time{4, 4};
    // Octave transposition of the written pitch (treble-8vb is -1): the sound
    // is the written pitch shifted by this many octaves. Zero is concert.
    // Callers must keep transposed notes inside MIDI [0, 127].
    int transpose_octaves = 0;
    std::vector<Measure> measures{};
};

struct Part {
    std::string name{};
    std::vector<Staff> staves{}; // one staff, or two+ for grand staff
};

struct Score {
    std::string title{};
    std::string composer{};
    std::vector<Part> parts{};
};

} // namespace blokkily::notation
