#pragma once

// The score hierarchy: measures hold staff objects in time order, staves hold
// measures under one clef/key/meter, parts group staves (a piano part has two),
// and a score groups parts under a title.

#include "blokkily/notation/clef.hpp"
#include "blokkily/notation/note.hpp"
#include "blokkily/notation/signature.hpp"

#include <optional>
#include <string>
#include <vector>

namespace blokkily::notation {

// How written pitch sounds: shifted by `semitones` and then by `octaves`.
struct Transposition {
    int semitones = 0;
    int octaves = 0;
    [[nodiscard]] int total() const noexcept { return semitones + 12 * octaves; }
    bool operator==(const Transposition&) const = default;
};

// A measure's objects, voice by voice. Inside one voice the objects follow
// each other in time; a note with `chord` set starts with the note before
// it. Voices are independent timelines from the measure's start, as
// MusicXML writes them, so a held note in voice 2 under moving notes in
// voice 1 keeps its own onset.
struct Measure {
    std::vector<StaffObject> objects{};
    // A change written at this measure: it holds from here until the next.
    std::optional<Clef> clef{};
    std::optional<KeySignature> key{};
    std::optional<TimeSignature> time{};
    // How the written pitch sounds from here on, as the staff's own
    // transposition says it for the measures before the first change.
    std::optional<Transposition> transpose{};
    // A pickup (anacrusis): the measure lasts as long as its notes, not a
    // whole bar of its meter.
    bool pickup = false;
};

struct Staff {
    // What holds from the first measure; a measure may change any of them.
    Clef clef{ClefKind::Treble};
    KeySignature key{};
    TimeSignature time{4, 4};
    // How the written pitch sounds: shifted by this many semitones and then
    // by this many octaves (a B-flat clarinet is -2 semitones; treble-8vb
    // is -1 octave). Zero is concert pitch.
    int transpose_semitones = 0;
    int transpose_octaves = 0;
    std::vector<Measure> measures{};
    [[nodiscard]] int transposition() const noexcept {
        return transpose_semitones + 12 * transpose_octaves;
    }
};

struct Part {
    std::string name{};
    std::vector<Staff> staves{}; // one staff, or two+ for grand staff
};

struct Score {
    std::string title{};
    std::string composer{};
    // Quarter notes per minute at the start, when the score says.
    std::optional<double> tempo_bpm{};
    std::vector<Part> parts{};
};

} // namespace blokkily::notation
