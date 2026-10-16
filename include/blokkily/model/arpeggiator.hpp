#pragma once

// A track's arpeggiator (features/arpeggiator.feature). While it is on, the
// notes held on the track - from the arrangement, the launcher, a keyboard or
// a MIDI port - are not played as chords: one of them sounds at a time, on a
// grid of `division` steps a beat, walking through them in `mode` order
// across `octaves` octaves, each held for `gate` of a step. Latched, the
// notes keep arpeggiating after they are let go, until a new chord is
// played. It sits before the instrument, so every instrument format hears
// its notes the same way.

#include <cstdint>

namespace blokkily {

struct Arpeggiator {
    enum class Mode : std::uint8_t { up = 0, down = 1, up_down = 2, played = 3, random = 4 };
    static constexpr std::uint8_t mode_count = 5;

    bool enabled = false;
    Mode mode = Mode::up;
    // Steps a beat: 1 quarters, 2 eighths, 3 eighth triplets, 4 sixteenths,
    // 6 sixteenth triplets, 8 thirty-seconds.
    std::uint8_t division = 4;
    std::uint8_t octaves = 1; // 1..4
    float gate = 0.5F;        // 0.05..1 of a step
    bool latch = false;

    friend bool operator==(const Arpeggiator&, const Arpeggiator&) = default;

    [[nodiscard]] static bool valid_division(int division) noexcept {
        return division == 1 || division == 2 || division == 3 || division == 4 ||
               division == 6 || division == 8;
    }
    [[nodiscard]] bool valid() const noexcept {
        return static_cast<std::uint8_t>(mode) < mode_count && valid_division(division) &&
               octaves >= 1 && octaves <= 4 && gate >= 0.05F && gate <= 1.0F;
    }
};

} // namespace blokkily
