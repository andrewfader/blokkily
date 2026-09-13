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

// How a cell is tiled. A honeycomb is not decoration: on a hexagonal grid every
// neighbour of a key is one step away in some interval, which is the whole
// argument for an isomorphic layout.
enum class CellShape { rectangle, hexagon };

// Which way the surface runs. Horizontal is the quarter turn a piano is drawn
// in; vertical is the same surface turned counter-clockwise, so pitch that ran
// to the right runs upward and the keyboard sits beside a tall editor.
enum class KeyboardOrientation { horizontal, vertical };

// Isomorphic layouts are said in cents per step, so the shape of a chord stays
// the same in any tuning rather than only in twelve tones.
struct IsomorphicLayout {
    std::string name = "Wicki-Hayden";
    double column_cents = 200.0;
    double row_cents = 700.0;
    bool stagger = true;
    CellShape shape = CellShape::hexagon;
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
    // Where the cell sits, in cell widths and heights from the top-left corner
    // of the surface. A square grid puts these at the column and the row; a
    // staggered or hexagonal layout shifts alternate rows by half a cell and
    // overlaps them, and a vertical surface has already been turned. Drawing
    // and hit-testing read these rather than the row and column, which stay
    // what they always were: which key this is, not where it is painted.
    double x = 0.0;
    double y = 0.0;
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
    // Which way the whole surface runs, whichever kind it is.
    KeyboardOrientation orientation = KeyboardOrientation::horizontal;
};

// How much room a laid-out surface needs, in cell widths and heights.
// Fractional, because staggered rows are half a cell wider than their contents
// and hexagonal rows overlap.
struct KeyboardExtent {
    double columns = 0.0;
    double rows = 0.0;
};

[[nodiscard]] std::vector<IsomorphicLayout> isomorphic_layouts();
[[nodiscard]] std::vector<KeyboardRegister> keyboard_registers();
[[nodiscard]] std::vector<StringTuning> string_tunings();

// Every cell of the surface, in reading order. Row zero is the top row as the
// interface draws it.
[[nodiscard]] std::vector<KeyboardCell> keyboard_cells(const KeyboardSpec& spec);

// The tiling this surface is drawn in. Only an isomorphic grid is ever a
// honeycomb; every other surface is named by the rows and columns it has.
[[nodiscard]] CellShape keyboard_shape(const KeyboardSpec& spec);
[[nodiscard]] KeyboardExtent keyboard_extent(const std::vector<KeyboardCell>& cells);

} // namespace blokkily
