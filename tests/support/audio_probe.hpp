#pragma once

// Measurements taken from rendered audio. An audio claim is proved from what
// the engine wrote into its output, never from the event or flag that asked
// for it, so every test reads the samples through these.
//
// Header-only and allocation-free: every probe may be called inside an armed
// allocation guard, right next to the process() call it measures.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <span>

namespace blokkily::probe {

// Root-mean-square level. Silence, and an empty block, is 0.
[[nodiscard]] inline double rms(std::span<const float> samples) noexcept {
    if (samples.empty()) return 0.0;
    double sum = 0.0;
    for (const float sample : samples) sum += static_cast<double>(sample) * sample;
    return std::sqrt(sum / static_cast<double>(samples.size()));
}

// The largest absolute sample value.
[[nodiscard]] inline float peak(std::span<const float> samples) noexcept {
    float loudest = 0.0F;
    for (const float sample : samples) loudest = std::max(loudest, std::abs(sample));
    return loudest;
}

// How many times the signal crosses upward through `threshold`: a sample above
// it whose predecessor is at or below it. The signal is taken to be silent
// before its first sample, so a note already sounding at sample zero counts
// as one edge. With the default threshold a sine counts its cycles; with a
// level threshold a gate counts its notes.
[[nodiscard]] inline std::size_t rising_edges(std::span<const float> samples,
                                              float threshold = 0.0F) noexcept {
    std::size_t edges = 0;
    bool below = 0.0F <= threshold;
    for (const float sample : samples) {
        const bool now_below = sample <= threshold;
        if (below && !now_below) ++edges;
        below = now_below;
    }
    return edges;
}

// The energy of one frequency, by the Goertzel algorithm, scaled so that a
// sine of amplitude A at exactly that frequency (a whole number of cycles in
// the block) measures A * A. Frequencies with nothing in them measure near 0.
[[nodiscard]] inline double goertzel_energy(std::span<const float> samples, double sample_rate,
                                            double frequency) noexcept {
    if (samples.empty() || !(sample_rate > 0.0)) return 0.0;
    const double coefficient =
        2.0 * std::cos(2.0 * std::numbers::pi * frequency / sample_rate);
    double s1 = 0.0;
    double s2 = 0.0;
    for (const float sample : samples) {
        const double s0 = static_cast<double>(sample) + coefficient * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const double power = s1 * s1 + s2 * s2 - coefficient * s1 * s2;
    const double scale = 2.0 / static_cast<double>(samples.size());
    return std::max(0.0, power) * scale * scale;
}

// The frequency between `low_hz` and `high_hz`, in steps of `step_hz`, whose
// Goertzel energy is greatest. The answer is only as fine as the step and as
// the block is long: a block of N samples cannot tell apart frequencies closer
// than sample_rate / N. Returns 0 for a silent or empty block or a bad range.
[[nodiscard]] inline double dominant_frequency(std::span<const float> samples,
                                               double sample_rate, double low_hz,
                                               double high_hz, double step_hz = 1.0) noexcept {
    if (samples.empty() || !(step_hz > 0.0) || !(high_hz >= low_hz)) return 0.0;
    double best_frequency = 0.0;
    double best_energy = 0.0;
    for (double frequency = low_hz; frequency <= high_hz; frequency += step_hz) {
        const double energy = goertzel_energy(samples, sample_rate, frequency);
        if (energy > best_energy) {
            best_energy = energy;
            best_frequency = frequency;
        }
    }
    return best_frequency;
}

// The index of the first sample whose magnitude exceeds `threshold`, or
// nothing if the block never does. With the default threshold this is where
// sound starts, to the sample.
[[nodiscard]] inline std::optional<std::size_t> first_nonzero(std::span<const float> samples,
                                                              float threshold = 0.0F) noexcept {
    for (std::size_t index = 0; index < samples.size(); ++index)
        if (std::abs(samples[index]) > threshold) return index;
    return std::nullopt;
}

} // namespace blokkily::probe
