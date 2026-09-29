#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace blokkily {

using Tick = std::int64_t;
using EventId = std::uint64_t;

struct Note {
    std::int16_t key = 60;
    float velocity = 0.8F;
    float release_velocity = 0.0F;
    // Retune from that key, so a note of a nineteen-tone scale is still one
    // note rather than an approximation of one.
    double cents = 0.0;
};

struct Chord {
    std::int16_t root = 60;
    std::vector<std::int16_t> intervals{0, 4, 7};
    std::int8_t inversion = 0;
    Tick strum = 0;
    // Retune per interval, so a chord of a nineteen-tone scale sounds the
    // chord it is rather than the nearest twelve-tone approximation of it.
    // Empty means every voice sits on its key, as twelve tones do.
    std::vector<double> cents;
    // How hard the chord is struck when its voices are not struck apart: a
    // chord pad presses every voice at once, at one velocity.
    float velocity = 0.8F;
    // One velocity per interval, for a chord whose voices were struck apart —
    // a take merged onto a step keeps how hard each key was played. Empty
    // means every voice at `velocity`.
    std::vector<float> velocities{};
    // One length per interval, in ticks, for voices held for different times.
    // Empty means every voice lasts as long as the trigger does.
    std::vector<Tick> durations{};
};

// How hard voice `interval` of a chord sounds, and for how long, read the one
// way the scheduler, the take recorder and the editors all read it.
[[nodiscard]] inline float voice_velocity(const Chord& chord, std::size_t interval) noexcept {
    return interval < chord.velocities.size() ? chord.velocities[interval] : chord.velocity;
}
[[nodiscard]] inline Tick voice_duration(const Chord& chord, std::size_t interval,
                                         Tick trigger_duration) noexcept {
    return interval < chord.durations.size() ? chord.durations[interval] : trigger_duration;
}

// A per-step parameter change. Automation and modulation stay distinct all the
// way to the plugin: automation sets the parameter's value, modulation offsets
// it without disturbing the value automation last wrote.
struct ParameterLock {
    enum class Kind { automation, modulation };
    std::string parameter_id;
    std::int32_t parameter_index = 0;
    double value = 0.0;
    Kind kind = Kind::automation;
};

// A controller movement in a pattern (wave 4.1): the pitch wheel, a control
// change, channel pressure or one key's poly pressure (aftertouch), played on
// a keyboard and recorded, at a tick of the pattern. Poly pressure names its
// key in `controller`: the key the instrument is told (after the song's
// tuning maps the keyboard), so it reaches the note it pressed. It is MIDI for the instrument to interpret, and stays distinct
// from parameter automation and parameter modulation all the way to the
// plugin (PluginEvent::Type::midi_raw). The sustain pedal is not one of
// these: the input holds note-offs while it is down (MidiInput), so what is
// recorded is how long each note was heard.
struct ContinuousEvent {
    enum class Kind : std::uint8_t { pitch_bend, control_change, channel_pressure, poly_pressure };
    Tick tick = 0;
    Kind kind = Kind::pitch_bend;
    std::uint8_t controller = 0;   // the CC number, or the key for poly_pressure
    std::uint16_t value = 8192;    // pitch bend 0..16383 (8192 centred); else 0..127

    friend bool operator==(const ContinuousEvent&, const ContinuousEvent&) = default;
    // What the value sits at when nothing has moved it, for the controllers
    // that have one: the wheel centred, no pressure, mod wheel, sustain and
    // breath at zero, expression full.
    [[nodiscard]] static std::optional<std::uint16_t> rest_value(Kind kind,
                                                                 std::uint8_t controller) noexcept {
        switch (kind) {
        case Kind::pitch_bend: return std::uint16_t{8192};
        case Kind::channel_pressure:
        case Kind::poly_pressure: return std::uint16_t{0};
        case Kind::control_change:
            if (controller == 1 || controller == 2 || controller == 64) return std::uint16_t{0};
            if (controller == 11) return std::uint16_t{127};
            return std::nullopt;
        }
        return std::nullopt;
    }
};

// A continuous event as three MIDI bytes packed like PluginEvent::midi_raw
// (status | data1 << 8 | data2 << 16), on MIDI channel 1: the track is the
// instrument, so the channel it was played on carries nothing.
[[nodiscard]] inline std::uint32_t midi_raw_of(const ContinuousEvent& event) noexcept {
    switch (event.kind) {
    case ContinuousEvent::Kind::pitch_bend:
        return 0xE0U | ((event.value & 0x7FU) << 8) | (((event.value >> 7) & 0x7FU) << 16);
    case ContinuousEvent::Kind::control_change:
        return 0xB0U | ((event.controller & 0x7FU) << 8) | ((event.value & 0x7FU) << 16);
    case ContinuousEvent::Kind::channel_pressure:
        return 0xD0U | ((event.value & 0x7FU) << 8);
    case ContinuousEvent::Kind::poly_pressure:
        return 0xA0U | ((event.controller & 0x7FU) << 8) | ((event.value & 0x7FU) << 16);
    }
    return 0;
}

// The continuous event three raw MIDI bytes are, at `tick`, or nothing for a
// message that is not one (notes, the sustain pedal, the all-notes-off
// family).
[[nodiscard]] inline std::optional<ContinuousEvent> continuous_from_midi(std::uint32_t raw,
                                                                         Tick tick) noexcept {
    const auto status = raw & 0xF0U;
    const auto first = static_cast<std::uint8_t>((raw >> 8) & 0x7FU);
    const auto second = static_cast<std::uint8_t>((raw >> 16) & 0x7FU);
    if (status == 0xE0U)
        return ContinuousEvent{tick, ContinuousEvent::Kind::pitch_bend, 0,
                               static_cast<std::uint16_t>(first | (second << 7))};
    if (status == 0xD0U)
        return ContinuousEvent{tick, ContinuousEvent::Kind::channel_pressure, 0, first};
    if (status == 0xA0U)
        return ContinuousEvent{tick, ContinuousEvent::Kind::poly_pressure, first, second};
    if (status == 0xB0U && first != 64 && first < 120)
        return ContinuousEvent{tick, ContinuousEvent::Kind::control_change, first, second};
    return std::nullopt;
}

// Per-note expression (wave 4.1, MPE): how one voice of a step moves while it
// sounds - its pitch, its timbre and its pressure, as an MPE keyboard plays
// them on the note's own channel. `voice` is the voice of the step, in the
// order the scheduler voices it (0 for a note; for a chord, after its
// inversion), and `offset` how many ticks after that voice starts the value
// arrives. Pitch is in semitones from the voice's tuned pitch (-96..96),
// timbre and pressure 0..1. It stays with the note: moved, copied, looped
// and cut with it, and distinct from controller movements (which are the
// whole instrument's), parameter automation and modulation.
struct NoteExpression {
    enum class Kind : std::uint8_t { pitch = 1, timbre = 2, pressure = 3 };
    std::uint8_t voice = 0;
    Tick offset = 0;
    Kind kind = Kind::pitch;
    float value = 0.0F;

    friend bool operator==(const NoteExpression&, const NoteExpression&) = default;
    [[nodiscard]] static bool valid_value(Kind kind, float value) noexcept {
        if (!(value == value)) return false; // NaN
        if (kind == Kind::pitch) return value >= -96.0F && value <= 96.0F;
        return value >= 0.0F && value <= 1.0F;
    }
};

struct Trigger {
    EventId id = 0;
    Tick start = 0;
    Tick duration = 120;
    Tick micro_offset = 0;
    std::variant<Note, Chord> musical_data = Note{};
    float probability = 1.0F;
    std::uint8_t ratchets = 1;
    std::uint8_t play_on_loop = 0; // zero means every loop
    std::vector<ParameterLock> locks;
    // Per-note expression of its voices, by voice and then offset.
    std::vector<NoteExpression> expression;
};

struct ScheduledNote {
    EventId source_id;
    Tick start;
    Tick duration;
    std::int16_t key;
    float velocity;
    double cents = 0.0;
    // The note carries per-note expression (NoteExpression).
    bool expressive = false;
};

// One value of a note's expression, where it sounds: addressed to the note by
// its key and retune.
struct ScheduledExpression {
    Tick start;
    std::int16_t key;
    double cents;
    NoteExpression::Kind kind;
    float value;
};

struct ScheduledParameter {
    EventId source_id;
    Tick start;
    std::int32_t index;
    double value;
    ParameterLock::Kind kind;
};

struct ScheduledContinuous {
    Tick start;
    ContinuousEvent event;
};

struct ScheduledEvents {
    std::vector<ScheduledNote> notes;
    std::vector<ScheduledParameter> parameters;
    std::vector<ScheduledContinuous> continuous;
    std::vector<ScheduledExpression> expressions;
};

} // namespace blokkily

