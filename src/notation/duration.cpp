#include "blokkily/notation/duration.hpp"

#include <algorithm>

namespace blokkily::notation {

namespace {

Tick unit_128th(Tick ticks_per_beat) {
    return std::max<Tick>(1, (ticks_per_beat * 4) / 128);
}

// Every (value, dots) with dots in [0, 2], largest tick count first.
std::vector<std::pair<NoteValue, int>> candidates(Tick ticks_per_beat) {
    std::vector<std::pair<NoteValue, int>> out;
    for (int v = -1; v <= 7; ++v)
        for (int d = 0; d <= 2; ++d)
            out.emplace_back(static_cast<NoteValue>(v), d);
    std::sort(out.begin(), out.end(), [&](const auto& a, const auto& b) {
        return ticks(NoteDuration{a.first, a.second}, ticks_per_beat) >
               ticks(NoteDuration{b.first, b.second}, ticks_per_beat);
    });
    return out;
}

} // namespace

std::optional<NoteDuration> single_from_ticks(Tick duration, Tick ticks_per_beat) {
    if (duration <= 0) throw std::invalid_argument("duration must be positive");
    for (const auto& [value, dots] : candidates(ticks_per_beat)) {
        if (ticks(NoteDuration{value, dots}, ticks_per_beat) == duration)
            return NoteDuration{value, dots};
    }
    return std::nullopt;
}

std::vector<NoteDuration> from_ticks(Tick duration, Tick ticks_per_beat) {
    if (duration <= 0) throw std::invalid_argument("duration must be positive");
    if (ticks_per_beat <= 0) throw std::invalid_argument("ticks_per_beat must be positive");
    const Tick unit = unit_128th(ticks_per_beat);
    Tick remaining = ((duration + unit / 2) / unit) * unit; // nearest grid
    if (remaining == 0) remaining = unit;
    std::vector<NoteDuration> chain;
    // For tied chains, prefer ties over dotted wholes/breves: a dotted whole
    // (6 beats) rarely fits a measure, so break it into whole + half, etc.
    // Dots are still allowed on half notes and smaller.
    const auto all_cands = candidates(ticks_per_beat);
    std::vector<std::pair<NoteValue, int>> cands;
    for (const auto& [value, dots] : all_cands) {
        const int v = static_cast<int>(value);
        if (dots > 0 && v <= 0) continue; // no dotted whole/breve in chains
        cands.emplace_back(value, dots);
    }
    while (remaining > 0) {
        bool placed = false;
        for (const auto& [value, dots] : cands) {
            const Tick t = ticks(NoteDuration{value, dots}, ticks_per_beat);
            if (t <= remaining && t > 0) {
                chain.push_back(NoteDuration{value, dots});
                remaining -= t;
                placed = true;
                break;
            }
        }
        if (!placed) break; // unreachable: the undotted 128th always fits
    }
    return chain;
}

} // namespace blokkily::notation
