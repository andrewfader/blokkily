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
};

struct ScheduledNote {
    EventId source_id;
    Tick start;
    Tick duration;
    std::int16_t key;
    float velocity;
    double cents = 0.0;
};

struct ScheduledParameter {
    EventId source_id;
    Tick start;
    std::int32_t index;
    double value;
    ParameterLock::Kind kind;
};

struct ScheduledEvents {
    std::vector<ScheduledNote> notes;
    std::vector<ScheduledParameter> parameters;
};

} // namespace blokkily

