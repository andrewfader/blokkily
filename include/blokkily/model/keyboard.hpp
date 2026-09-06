#pragma once

#include "blokkily/model/scale.hpp"
#include "blokkily/model/tuning.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace blokkily {

// The playable surfaces. They differ only in how pitch is laid out under the
// hands: every one of them produces the same kind of cell, so playing, drawing,
// and testing are the same job for all of them.
enum class KeyboardKind { piano, isomorphic, fretboard, theoryboard };

// Isomorphic layouts are said in cents per step, so the shape of a chord stays
// the same in any tuning rather than only in twelve tones.
struct IsomorphicLayout {
    std::string name = "Wicki-Hayden";
    double column_cents = 200.0;
    double row_cents = 700.0;
    bool stagger = true;
};

// A range of the keyboard, named the way a singer or a score names it.
struct KeyboardRegister {
    std::string name = "Full";
    int lowest_key = 36;
    int keys = 49;
};

struct StringTuning {
    std::string name = "Guitar (standard)";
    std::vector<int> strings{40, 45, 50, 55, 59, 64}; // lowest first, twelve-tone keys
};

// One playable cell: which degree it sounds, what an instrument must be told to
// sound it, where it sits, and what it is called.
struct KeyboardCell {
    int degree = 60;
    TunedPitch pitch{};
    int row = 0;
    int column = 0;
    bool accidental = false;
    bool in_scale = true;
    bool root = false;
    std::string label;
    // A theoryboard pad sounds a chord rather than a note; for every other
    // surface this holds the single degree the cell plays.
    std::vector<int> chord;
};

struct KeyboardSpec {
    KeyboardKind kind = KeyboardKind::piano;
    Tuning tuning{};
    Scale scale{};
    int root_degree = 60;
    KeyboardRegister range{};
    // Isomorphic grids.
    IsomorphicLayout isomorphic{};
    int rows = 4;
    int columns = 12;
    // Fretboards.
    StringTuning strings{};
    int frets = 12;
    // Theoryboards.
    int voices = 3;
    int inversions = 3;
};

[[nodiscard]] std::vector<IsomorphicLayout> isomorphic_layouts();
[[nodiscard]] std::vector<KeyboardRegister> keyboard_registers();
[[nodiscard]] std::vector<StringTuning> string_tunings();

// Every cell of the surface, in reading order. Row zero is the top row as the
// interface draws it.
[[nodiscard]] std::vector<KeyboardCell> keyboard_cells(const KeyboardSpec& spec);

} // namespace blokkily
