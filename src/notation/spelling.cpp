#include "blokkily/notation/spelling.hpp"

#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace blokkily::notation {

SpelledPitch spell(int midi_key, const KeySignature& key) {
    if (midi_key < 0 || midi_key > 127)
        throw std::invalid_argument("MIDI key must be in [0, 127]");
    const int pc = midi_key % 12;
    const int octave = midi_key / 12 - 1;

    struct Candidate {
        Step step;
        Accidental accidental;
    };
    std::vector<Candidate> candidates;
    for (int s = 0; s < 7; ++s) {
        for (int a = -2; a <= 2; ++a) {
            const int spelled_pc = (step_semitones(static_cast<Step>(s)) + a) % 12;
            if ((spelled_pc + 12) % 12 == pc)
                candidates.push_back({static_cast<Step>(s), static_cast<Accidental>(a)});
        }
    }

    // The key's own spelling of this pitch class wins outright.
    for (const auto& c : candidates)
        if (key.accidental_for(c.step) == c.accidental)
            return SpelledPitch{c.step, c.accidental, octave};

    // Fewest accidentals; ties go sharp-side in sharp keys and C major,
    // flat-side in flat keys.
    const bool sharp_side = key.fifths() >= 0;
    const Candidate* best = nullptr;
    for (const auto& c : candidates) {
        if (best == nullptr) {
            best = &c;
            continue;
        }
        const int ca = std::abs(static_cast<int>(c.accidental));
        const int ba = std::abs(static_cast<int>(best->accidental));
        if (ca < ba) {
            best = &c;
        } else if (ca == ba) {
            const bool c_sharp = static_cast<int>(c.accidental) > 0;
            const bool b_sharp = static_cast<int>(best->accidental) > 0;
            if (c_sharp == sharp_side && b_sharp != sharp_side) best = &c;
        }
    }
    return SpelledPitch{best->step, best->accidental, octave};
}

} // namespace blokkily::notation
