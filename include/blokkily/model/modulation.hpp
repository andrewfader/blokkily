#pragma once

// Modulators (phase 2, wave 5.1): LFOs, macros and envelope followers that
// move plugin parameters while the song plays. They belong to the song like
// every other mixer setting, so they are saved, undone and validated with it,
// and the engine compiles them from the song rather than keeping its own copy.
//
// What a modulator sends is a modulation, never automation: an offset added
// to the parameter's automated (or dialled) value, which the plugin forgets
// the moment the modulation goes. The offset a target receives is
//
//     signal x depth x (parameter maximum - parameter minimum)
//
// where the signal is the modulator's output (an LFO swings -1..+1, a macro
// sits at its value 0..1, a follower tracks its source track's level 0..1)
// and `depth` (-1..+1) is the target's share of the parameter's range. Several
// modulators on one parameter add up.

#include "blokkily/model/processor_address.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace blokkily {

enum class LfoShape : std::uint8_t { sine, triangle, saw_up, saw_down, square, sample_and_hold };

// One parameter a modulator moves: the processor (a track's instrument, slot
// -1, or an insert on any bus) and the parameter's index, the same numbering
// automation and parameter locks use.
struct ModulationTarget {
    ProcessorAddress processor{};
    std::int32_t parameter_index = 0;
    double depth = 0.5;

    friend bool operator==(const ModulationTarget&, const ModulationTarget&) = default;
};

struct Modulator {
    enum class Kind : std::uint8_t { lfo, macro, follower };

    Kind kind = Kind::lfo;
    std::string name = "LFO";
    // LFO: its shape and speed. `sync_beats` > 0 locks one cycle to that many
    // beats of the tempo map; 0 runs free at `rate_hz`.
    LfoShape shape = LfoShape::sine;
    double rate_hz = 1.0;
    double sync_beats = 0.0;
    // Macro: the knob, 0..1.
    double value = 0.0;
    // Follower: the track whose level it follows (after its inserts, before
    // its fader), and how fast it rises and falls.
    std::uint32_t source_track = 0;
    double attack_ms = 10.0;
    double release_ms = 120.0;
    std::vector<ModulationTarget> targets;

    static constexpr std::size_t maximum_targets = 8;
    static constexpr double minimum_rate_hz = 0.01;
    static constexpr double maximum_rate_hz = 40.0;
    static constexpr double maximum_sync_beats = 64.0;
    static constexpr double minimum_time_ms = 0.1;
    static constexpr double maximum_time_ms = 5000.0;

    friend bool operator==(const Modulator&, const Modulator&) = default;
};

// The most modulators a song holds: the engine keeps live controls for this
// many without allocating while it plays.
inline constexpr std::size_t maximum_modulators = 32;

} // namespace blokkily
