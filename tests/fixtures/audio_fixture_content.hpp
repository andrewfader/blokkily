#pragma once

// What the generated audio fixtures contain, defined once so the generator
// (tests/fixtures/make_audio_fixtures.cpp) and the tests that read the files
// back agree on every sample without any binary being checked in.

#include <cmath>
#include <cstdint>

namespace blokkily::audio_fixtures {

inline constexpr double two_pi = 6.283185307179586476925286766559;

// sine1k_44k1_pcm16.wav, sine1k_44k1.flac and the left channel of
// sine1k_44k1_stereo.aiff (whose right channel is its negation): one second
// of 1 kHz at half scale, 16-bit, 44.1 kHz.
inline constexpr std::uint32_t sine1k_rate = 44100;
inline constexpr std::uint64_t sine1k_frames = 44100;
inline constexpr double sine1k_hz = 1000.0;
inline std::int16_t sine1k(std::uint64_t frame) {
    return static_cast<std::int16_t>(
        std::lrint(16383.0 * std::sin(two_pi * sine1k_hz * static_cast<double>(frame) / sine1k_rate)));
}

// step_44k1_float.wav: silence, then a DC step to 0.5 at step_frame. The
// step is the timing marker a resampled copy must keep in place.
inline constexpr std::uint32_t step_rate = 44100;
inline constexpr std::uint64_t step_frames = 44100;
inline constexpr std::uint64_t step_frame = 22050;
inline float step(std::uint64_t frame) { return frame < step_frame ? 0.0F : 0.5F; }

// stereo_48k_pcm24.wav: 440 Hz left at a quarter scale, 880 Hz right at three
// quarters, 24-bit.
inline constexpr std::uint32_t stereo24_rate = 48000;
inline constexpr std::uint64_t stereo24_frames = 4800;
inline std::int32_t stereo24(std::uint64_t frame, int channel) {
    const double hz = channel == 0 ? 440.0 : 880.0;
    const double level = channel == 0 ? 0.25 : 0.75;
    return static_cast<std::int32_t>(std::lrint(
        level * 8388607.0 * std::sin(two_pi * hz * static_cast<double>(frame) / stereo24_rate)));
}

// extensible_float_48k_stereo.wav: WAVE_FORMAT_EXTENSIBLE with the IEEE float
// subformat. Left 300 Hz at 0.3; right is left times -0.5.
inline constexpr std::uint32_t extensible_rate = 48000;
inline constexpr std::uint64_t extensible_frames = 4800;
inline float extensible(std::uint64_t frame, int channel) {
    const auto left = static_cast<float>(
        0.3 * std::sin(two_pi * 300.0 * static_cast<double>(frame) / extensible_rate));
    return channel == 0 ? left : left * -0.5F;
}

// smpl_loop_44k1_pcm16.wav: 440 Hz with a `smpl` chunk naming MIDI unity
// note 57 and one forward loop over frames 1000..2999 inclusive.
inline constexpr std::uint32_t smpl_rate = 44100;
inline constexpr std::uint64_t smpl_frames = 4410;
inline constexpr int smpl_root_key = 57;
inline constexpr std::uint32_t smpl_loop_first = 1000;
inline constexpr std::uint32_t smpl_loop_last = 2999;
inline std::int16_t smpl_sample(std::uint64_t frame) {
    return static_cast<std::int16_t>(
        std::lrint(8000.0 * std::sin(two_pi * 440.0 * static_cast<double>(frame) / smpl_rate)));
}

// hits8_48k_pcm16.wav: eight hits, one every hit_spacing frames starting at
// frame 0, each a 64-frame decaying burst at full scale, silence between.
inline constexpr std::uint32_t hits8_rate = 48000;
inline constexpr std::uint64_t hits8_frames = 48000;
inline constexpr std::uint64_t hit_spacing = 6000;
inline constexpr std::uint64_t hit_length = 64;
inline std::int16_t hits8(std::uint64_t frame) {
    const std::uint64_t within = frame % hit_spacing;
    if (frame >= 8 * hit_spacing || within >= hit_length) return 0;
    const double decay = 1.0 - static_cast<double>(within) / hit_length;
    const double sign = within % 2 == 0 ? 1.0 : -1.0;
    return static_cast<std::int16_t>(std::lrint(32000.0 * decay * sign));
}

// tones8_48k_pcm16.wav (the sampler's slice source, item 1.9): eight hits,
// one every tones8_spacing frames from frame 0. Hit i is tones8_hit_length
// frames of a tones8_hz(i) sine at half scale, silence between, so each slice
// of the loop names itself by its frequency.
inline constexpr std::uint32_t tones8_rate = 48000;
inline constexpr std::uint64_t tones8_frames = 48000;
inline constexpr std::uint64_t tones8_spacing = 6000;
inline constexpr std::uint64_t tones8_hit_length = 1440;   // 30 ms
inline constexpr double tones8_hz(std::uint64_t hit) { return 200.0 * static_cast<double>(hit + 1); }
inline std::int16_t tones8(std::uint64_t frame) {
    const std::uint64_t hit = frame / tones8_spacing;
    const std::uint64_t within = frame % tones8_spacing;
    if (hit >= 8 || within >= tones8_hit_length) return 0;
    return static_cast<std::int16_t>(std::lrint(
        16383.0 * std::sin(two_pi * tones8_hz(hit) * static_cast<double>(within) / tones8_rate)));
}

// loop480_48k_pcm16.wav: 480 Hz (exactly 100 frames a cycle) at half scale,
// with a `smpl` chunk naming unity note 71 and a forward loop over a whole
// number of cycles, frames 1200..3199 inclusive, so the loop seam is smooth.
inline constexpr std::uint32_t loop480_rate = 48000;
inline constexpr std::uint64_t loop480_frames = 4800;
inline constexpr int loop480_root_key = 71;
inline constexpr std::uint32_t loop480_loop_first = 1200;
inline constexpr std::uint32_t loop480_loop_last = 3199;
inline std::int16_t loop480(std::uint64_t frame) {
    return static_cast<std::int16_t>(
        std::lrint(16383.0 * std::sin(two_pi * static_cast<double>(frame % 100) / 100.0)));
}

// truncated_data.wav: a PCM16 mono header promising sine1k_frames frames,
// followed by only this many.
inline constexpr std::uint64_t truncated_frames_present = 1000;

} // namespace blokkily::audio_fixtures
