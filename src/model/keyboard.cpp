#include "blokkily/model/keyboard.hpp"

#include <algorithm>
#include <cmath>

namespace blokkily {
namespace {

// A piano draws black keys only where twelve tones put them. In any other
// tuning that pattern would be a lie, so the scale decides which keys are
// raised instead.
bool piano_accidental(const Tuning& tuning, const Scale& scale, int root_degree, int degree) {
    if (tuning.is_twelve_tone()) {
        const int pitch_class = ((degree % 12) + 12) % 12;
        return pitch_class == 1 || pitch_class == 3 || pitch_class == 6 ||
               pitch_class == 8 || pitch_class == 10;
    }
    return !in_scale(tuning, scale, root_degree, degree);
}

KeyboardCell make_cell(const KeyboardSpec& spec, int degree, int row, int column) {
    KeyboardCell cell;
    cell.degree = degree;
    cell.pitch = degree_pitch(spec.tuning, degree);
    cell.row = row;
    cell.column = column;
    cell.in_scale = in_scale(spec.tuning, spec.scale, spec.root_degree, degree);
    const int divisions = std::max(1, spec.tuning.divisions());
    cell.root = ((degree - spec.root_degree) % divisions) == 0;
    cell.accidental = piano_accidental(spec.tuning, spec.scale, spec.root_degree, degree);
    cell.label = degree_name(spec.tuning, degree);
    cell.chord = {degree};
    return cell;
}

// The degree nearest to a step of `cents` away, used by every isomorphic grid
// so the same layout works in any division of the octave.
int step_degrees(const Tuning& tuning, double cents) {
    const int divisions = std::max(1, tuning.divisions());
    return static_cast<int>(std::llround(cents / (tuning.period_cents / divisions)));
}

} // namespace

std::vector<IsomorphicLayout> isomorphic_layouts() {
    return {
        {"Wicki-Hayden", 200.0, 700.0, true},
        {"Jankó", 200.0, 100.0, true},
        {"Harmonic table", 700.0, 400.0, true},
        {"Fourths", 100.0, 500.0, false},
    };
}

std::vector<KeyboardRegister> keyboard_registers() {
    return {
        {"Bass", 28, 25},
        {"Tenor", 40, 25},
        {"Alto", 48, 25},
        {"Treble", 60, 25},
        {"Full", 36, 49},
    };
}

std::vector<StringTuning> string_tunings() {
    return {
        {"Guitar (standard)", {40, 45, 50, 55, 59, 64}},
        {"Guitar (drop D)", {38, 45, 50, 55, 59, 64}},
        {"Guitar (DADGAD)", {38, 45, 50, 55, 57, 62}},
        {"Bass (4-string)", {28, 33, 38, 43}},
        {"Bass (5-string)", {23, 28, 33, 38, 43}},
        {"Tenor guitar (CGDA)", {36, 43, 50, 57}},
        {"Tenor banjo (GDAE)", {43, 50, 57, 64}},
        {"Ukulele (GCEA)", {67, 60, 64, 69}},
        {"Mandolin (GDAE)", {55, 62, 69, 76}},
    };
}

std::vector<KeyboardCell> keyboard_cells(const KeyboardSpec& spec) {
    std::vector<KeyboardCell> cells;
    switch (spec.kind) {
        case KeyboardKind::piano: {
            // The register says which pitches are under the hands; in a tuning
            // with more degrees per octave that is more keys, not a wider span.
            const int lowest = degree_for_key(spec.tuning, spec.range.lowest_key);
            const int highest_key = spec.range.lowest_key + spec.range.keys - 1;
            const int highest = degree_for_key(spec.tuning, highest_key);
            cells.reserve(static_cast<std::size_t>(std::max(0, highest - lowest + 1)));
            for (int degree = lowest, column = 0; degree <= highest; ++degree, ++column)
                cells.push_back(make_cell(spec, degree, 0, column));
            return cells;
        }
        case KeyboardKind::isomorphic: {
            const int column_step = step_degrees(spec.tuning, spec.isomorphic.column_cents);
            const int row_step = step_degrees(spec.tuning, spec.isomorphic.row_cents);
            const int lowest = degree_for_key(spec.tuning, spec.range.lowest_key);
            for (int row = 0; row < std::max(1, spec.rows); ++row)
                for (int column = 0; column < std::max(1, spec.columns); ++column) {
                    // Row zero is the top row, so higher rows sound higher.
                    const int height = std::max(1, spec.rows) - 1 - row;
                    cells.push_back(make_cell(spec, lowest + height * row_step +
                                                        column * column_step, row, column));
                }
            return cells;
        }
        case KeyboardKind::fretboard: {
            const auto& strings = spec.strings.strings;
            for (int index = static_cast<int>(strings.size()) - 1; index >= 0; --index) {
                // The highest string is drawn on top, the way a fretboard is
                // read rather than the way the tuning lists it.
                const int row = static_cast<int>(strings.size()) - 1 - index;
                const int open = degree_for_key(spec.tuning, strings[static_cast<std::size_t>(index)]);
                for (int fret = 0; fret <= std::max(0, spec.frets); ++fret)
                    cells.push_back(make_cell(spec, open + fret, row, fret));
            }
            return cells;
        }
        case KeyboardKind::theoryboard: {
            const auto degrees = scale_degrees(spec.tuning, spec.scale, spec.root_degree);
            const int steps = degrees.empty() ? 7 : static_cast<int>(degrees.size());
            const int rows = std::max(1, spec.inversions);
            for (int row = 0; row < rows; ++row)
                for (int step = 0; step < steps; ++step) {
                    // Each row is the same chord one inversion further up, so a
                    // column is one harmony and a row is one voicing.
                    const auto chord = scale_chord(spec.tuning, spec.scale, spec.root_degree,
                                                   step, spec.voices, row);
                    if (chord.degrees.empty()) continue;
                    auto cell = make_cell(spec, chord.degrees.front(), row, step);
                    cell.chord = chord.degrees;
                    cell.label = chord.name;
                    cell.in_scale = true;
                    cell.accidental = chord.quality == "diminished" ||
                                      chord.quality == "augmented";
                    cell.root = step == 0;
                    cells.push_back(std::move(cell));
                }
            return cells;
        }
    }
    return cells;
}

} // namespace blokkily
