#pragma once

// Enharmonic spelling: which written note a MIDI key becomes under a key
// signature. The spelling is diatonic to the key when the key has a spelling
// for that pitch class, otherwise the fewest accidentals win, with ties broken
// toward sharps in sharp keys (and C major) and toward flats in flat keys.

#include "blokkily/notation/pitch.hpp"
#include "blokkily/notation/signature.hpp"

namespace blokkily::notation {

[[nodiscard]] SpelledPitch spell(int midi_key, const KeySignature& key);

} // namespace blokkily::notation
