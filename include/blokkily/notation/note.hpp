#pragma once

// One sounding note or one rest, as theory writes it. A NotationNote is a
// spelled pitch plus a value; ties chain values into longer durations, lyrics
// hang one syllable under each note, and `cents` carries a microtonal retune
// that sounds but never changes the spelling.

#include "blokkily/notation/duration.hpp"
#include "blokkily/notation/pitch.hpp"

#include <string>
#include <variant>

namespace blokkily::notation {

struct NotationNote {
    SpelledPitch spelled{};
    NoteDuration duration{};
    bool tie_start = false; // tied into the next note
    bool tie_end = false;   // tied from the previous note
    // One lyric syllable. A trailing "-" continues the syllable as a melisma
    // onto the next note; a syllable of exactly "_" extends the previous
    // syllable under this note. Empty means no lyric.
    std::string lyric{};
    int voice = 1;       // 1-based, for multi-voice staves
    double cents = 0.0;  // microtonal retune: sounds, does not respell
    float velocity = 0.8F; // how hard the note is struck; round-trips the pattern
};

struct Rest {
    NoteDuration duration{};
};

using StaffObject = std::variant<NotationNote, Rest>;

[[nodiscard]] inline bool is_note(const StaffObject& object) noexcept {
    return std::holds_alternative<NotationNote>(object);
}

[[nodiscard]] inline bool is_rest(const StaffObject& object) noexcept {
    return std::holds_alternative<Rest>(object);
}

} // namespace blokkily::notation
