#pragma once

// What a sampler plays (design 2, plan item 1.9). Pure data plus its
// serialization: nothing here decodes audio or runs on the audio thread.
//
// One program covers both sampler modes. A keyed program is one or more
// pitch-tracking zones, each with a root key, a key and velocity range, a
// region of its file, a loop and an envelope. A drum (kit) program is one
// zone per key that does not track pitch and plays to its end whatever the
// note length. Slicing a loop is not a third engine: slice_evenly() writes N
// kit zones over one file, one per consecutive key.
//
// Frame positions (start, end, loop points) count frames of the sample file
// at its own rate, so a program means the same thing at every engine rate.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace blokkily {

enum class LoopMode : std::uint8_t { off, forward, ping_pong };

// Linear-segment ADSR. Times in seconds; sustain is a level, 0..1.
struct SamplerEnvelope {
    double attack_s = 0.002;
    double decay_s = 0.0;
    double sustain = 1.0;
    double release_s = 0.05;

    friend bool operator==(const SamplerEnvelope&, const SamplerEnvelope&) = default;
};

struct SamplerZone {
    // The file, as the user chose it: absolute, or relative to the project
    // directory (SamplerInstrument::set_base_directory resolves it).
    std::string sample;
    int root_key = 60;          // the key at which the file plays at its own pitch
    double fine_cents = 0.0;    // added to the root: +50 plays the file 50 cents flat
    int low_key = 0, high_key = 127;
    int low_velocity = 1, high_velocity = 127;   // MIDI velocity, 1..127
    std::uint64_t start = 0;
    std::uint64_t end = 0;      // one past the last frame played; 0 = the end of the file
    LoopMode loop = LoopMode::off;
    std::uint64_t loop_start = 0;
    std::uint64_t loop_end = 0;  // one past the last looped frame, like LoopPoints
    SamplerEnvelope envelope;
    double gain_db = 0.0;
    double pan = 0.0;           // -1 left .. +1 right (balance)
    bool track_pitch = true;    // false: every key plays the file at its own pitch
    bool one_shot = false;      // true: note_off is ignored; the zone plays to its end
    int choke_group = 0;        // 0 = none; a new note in a group cuts the others in it

    friend bool operator==(const SamplerZone&, const SamplerZone&) = default;
};

struct SamplerProgram {
    enum class Mode : std::uint8_t { keyed, kit };
    Mode mode = Mode::keyed;
    std::vector<SamplerZone> zones;
    int polyphony = 32;         // voices sounding at once, 1..sampler_max_voices

    friend bool operator==(const SamplerProgram&, const SamplerProgram&) = default;
};

inline constexpr int sampler_max_voices = 64;

// The version of the state blob this build writes. parse_sampler rejects any
// other version rather than reading part of it.
inline constexpr int sampler_state_version = 1;

// A deterministic text blob:
//   blokkily-sampler 1
//   mode keyed|kit
//   polyphony N
//   zone "<path>" root fine lo hi vlo vhi start end off|forward|ping_pong
//        loop_start loop_end attack decay sustain release gain pan
//        pitch oneshot choke                           (one line per zone)
// Doubles use the shortest form that reads back exactly, so the same program
// always serializes to the same bytes.
[[nodiscard]] std::vector<std::byte> serialize_sampler(const SamplerProgram& program);
[[nodiscard]] std::optional<SamplerProgram> parse_sampler(std::span<const std::byte> state,
                                                          std::string* error = nullptr);

// Whether every field of the program is in range: keys 0..127 with low <=
// high, velocities 1..127, a region and loop that end after they start, finite
// non-negative envelope times, sustain and pan in range, polyphony
// 1..sampler_max_voices. parse_sampler only returns programs that pass.
[[nodiscard]] bool validate_sampler(const SamplerProgram& program, std::string* error = nullptr);

// A program with no zones in the given mode.
[[nodiscard]] SamplerProgram default_sampler(SamplerProgram::Mode mode);

// Replaces the program's zones with `count` kit zones over `source`'s region
// (its start to its end, or to `source_frames` when its end is 0). Slice i
// covers [start + round(i * length / count), start + round((i + 1) * length /
// count)) and answers key first_key + i only. Slices play the file at its own
// pitch, as one-shots, in no choke group. The mode becomes kit. Returns false,
// changing nothing, when count is not 1..128, the keys would pass 127, or the
// region is shorter than `count` frames.
bool slice_evenly(SamplerProgram& program, const SamplerZone& source,
                  std::uint64_t source_frames, int count, int first_key);

// Every zone that answers a key at a MIDI velocity (1..127), in program order.
[[nodiscard]] std::vector<const SamplerZone*> zones_for(const SamplerProgram& program, int key,
                                                        int velocity);

} // namespace blokkily
