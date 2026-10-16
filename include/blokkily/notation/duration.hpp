#pragma once

// Note values from the breve down to the 128th, with dots. Tick conversion is
// against a ticks-per-beat; Blokkily's default 480 divides evenly down to the
// 128th (15 ticks). `from_ticks` decomposes any positive duration into a tied
// chain, largest first; a remainder smaller than a 128th rounds to the nearest
// representable grid (documented, and asserted in tests).

#include "blokkily/model/event.hpp" // Tick

#include <optional>
#include <stdexcept>
#include <vector>

namespace blokkily::notation {

// Halving steps from the whole note; the breve is one step above it.
enum class NoteValue : int {
    Breve = -1,
    Whole = 0,
    Half = 1,
    Quarter = 2,
    Eighth = 3,
    Sixteenth = 4,
    ThirtySecond = 5,
    SixtyFourth = 6,
    HundredTwentyEighth = 7
};

struct NoteDuration {
    NoteValue value = NoteValue::Quarter;
    int dots = 0;          // 0..2 in what from_ticks produces
    bool stemless = false; // chant noteheads: no stem is drawn
};

// Ticks of one (possibly dotted) value. Each dot adds half the previous
// addition, truncating — the integer analogue of the engraved rule.
[[nodiscard]] inline Tick ticks(NoteDuration duration, Tick ticks_per_beat) {
    if (ticks_per_beat <= 0) throw std::invalid_argument("ticks_per_beat must be positive");
    if (duration.dots < 0) throw std::invalid_argument("dots must be non-negative");
    const int v = static_cast<int>(duration.value);
    Tick base = (v < 0) ? ticks_per_beat * 8 : (ticks_per_beat * 4) >> v;
    Tick total = base;
    Tick add = base;
    for (int i = 0; i < duration.dots; ++i) {
        add /= 2;
        total += add;
    }
    return total;
}

[[nodiscard]] inline Tick ticks(const std::vector<NoteDuration>& chain, Tick ticks_per_beat) {
    Tick total = 0;
    for (const auto& d : chain) total += ticks(d, ticks_per_beat);
    return total;
}

// One value+dots for an exact duration, or nullopt when it is not exact.
[[nodiscard]] std::optional<NoteDuration> single_from_ticks(Tick duration, Tick ticks_per_beat);

// A tied chain for any positive duration, largest value first. The input is
// rounded to the nearest 128th-note grid first when it does not land on one.
[[nodiscard]] std::vector<NoteDuration> from_ticks(Tick duration, Tick ticks_per_beat);

[[nodiscard]] inline const char* value_name(NoteValue value) noexcept {
    switch (value) {
        case NoteValue::Breve: return "breve";
        case NoteValue::Whole: return "whole";
        case NoteValue::Half: return "half";
        case NoteValue::Quarter: return "quarter";
        case NoteValue::Eighth: return "eighth";
        case NoteValue::Sixteenth: return "sixteenth";
        case NoteValue::ThirtySecond: return "32nd";
        case NoteValue::SixtyFourth: return "64th";
        case NoteValue::HundredTwentyEighth: return "128th";
    }
    return "quarter";
}

} // namespace blokkily::notation
