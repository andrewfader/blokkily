#pragma once

// A pitch said the way theory says it: a diatonic step, an accidental, and an
// octave in scientific pitch notation (middle C is C4, MIDI 60). MIDI keys are
// the transport to and from the pattern model; the spelling is what the staff
// shows.

#include <stdexcept>
#include <string_view>

namespace blokkily::notation {

enum class Step : int { C = 0, D = 1, E = 2, F = 3, G = 4, A = 5, B = 6 };

enum class Accidental : int {
    DoubleFlat = -2,
    Flat = -1,
    Natural = 0,
    Sharp = 1,
    DoubleSharp = 2
};

struct SpelledPitch {
    Step step = Step::C;
    Accidental accidental = Accidental::Natural;
    int octave = 4; // scientific pitch notation: C4 is middle C
};

// The absolute diatonic position: C0 is 0, D0 is 1, ... B0 is 6, C1 is 7.
[[nodiscard]] constexpr int diatonic_index(const SpelledPitch& pitch) noexcept {
    return pitch.octave * 7 + static_cast<int>(pitch.step);
}

// Semitones of the natural step within its octave: C=0 ... B=11.
[[nodiscard]] constexpr int step_semitones(Step step) noexcept {
    constexpr int table[7] = {0, 2, 4, 5, 7, 9, 11};
    return table[static_cast<int>(step)];
}

// Spelled pitch -> MIDI key (C4 == 60).
[[nodiscard]] constexpr int to_midi(const SpelledPitch& pitch) noexcept {
    return (pitch.octave + 1) * 12 + step_semitones(pitch.step) +
           static_cast<int>(pitch.accidental);
}

// MIDI key -> spelled pitch, sharp-side spelling (C#, never Db). Key-aware
// spelling lives in spelling.hpp; this is the canonical fallback.
[[nodiscard]] inline SpelledPitch from_midi(int midi_key) {
    if (midi_key < 0 || midi_key > 127)
        throw std::invalid_argument("MIDI key must be in [0, 127]");
    constexpr Step steps[12] = {Step::C, Step::C, Step::D, Step::D, Step::E, Step::F,
                                Step::F, Step::G, Step::G, Step::A, Step::A, Step::B};
    constexpr Accidental accs[12] = {Accidental::Natural, Accidental::Sharp,
                                     Accidental::Natural, Accidental::Sharp,
                                     Accidental::Natural, Accidental::Natural,
                                     Accidental::Sharp,   Accidental::Natural,
                                     Accidental::Sharp,   Accidental::Natural,
                                     Accidental::Sharp,   Accidental::Natural};
    const int pc = midi_key % 12;
    return SpelledPitch{steps[pc], accs[pc], midi_key / 12 - 1};
}

[[nodiscard]] constexpr std::string_view step_name(Step step) noexcept {
    constexpr std::string_view names[7] = {"C", "D", "E", "F", "G", "A", "B"};
    return names[static_cast<int>(step)];
}

[[nodiscard]] constexpr std::string_view accidental_name(Accidental accidental) noexcept {
    switch (accidental) {
        case Accidental::DoubleFlat: return "bb";
        case Accidental::Flat: return "b";
        case Accidental::Natural: return "";
        case Accidental::Sharp: return "#";
        case Accidental::DoubleSharp: return "x";
    }
    return "";
}

} // namespace blokkily::notation
