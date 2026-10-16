#pragma once

// MIDI learn (features/midi_learn.feature): a hardware controller - one CC on
// one channel - bound to a mixer control or a macro. The binding belongs to
// the song, so a session opens with its controller map; moving the knob is
// an edit like moving the control on screen.

#include <cstdint>

namespace blokkily {

struct MidiBinding {
    enum class Target : std::uint8_t {
        track_gain = 0,  // `index` is the track; 0..127 sweeps -60..+6 dB
        track_pan = 1,   // left to right
        track_mute = 2,  // 64 and over is muted
        track_solo = 3,  // 64 and over is soloed
        master_gain = 4, // `index` unused; -60..+6 dB
        macro = 5,       // `index` is the modulator, a macro; 0..1
    };
    static constexpr std::uint8_t target_count = 6;

    std::uint8_t channel = 0;    // 0-based
    std::uint8_t controller = 0; // 0..119
    Target target = Target::track_gain;
    std::uint32_t index = 0;

    friend bool operator==(const MidiBinding&, const MidiBinding&) = default;
};

} // namespace blokkily
