// The probes every audio claim is read through, checked against signals whose
// answers are known in closed form. They run inside an armed guard, because a
// case may measure right next to the process() call it is proving.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include <cmath>
#include <numbers>
#include <optional>
#include <vector>

BLOKKILY_REALTIME_CASE(audio_probe) {
    using blokkily::realtime::require;
    using namespace blokkily::probe;

    constexpr double rate = 48000.0;
    // 4800 samples of 440 Hz is exactly 44 cycles, so the Goertzel bin is exact.
    std::vector<float> sine(4800);
    for (std::size_t index = 0; index < sine.size(); ++index)
        sine[index] = static_cast<float>(
            0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(index) / rate));
    // A gate: silence, then three pulses of a steady level.
    std::vector<float> gate(1000, 0.0F);
    for (const std::size_t start : {37U, 400U, 800U})
        for (std::size_t index = start; index < start + 50; ++index) gate[index] = 0.25F;
    const std::vector<float> silence(512, 0.0F);

    double sine_rms = 0.0, sine_on = 0.0, sine_off = 0.0, sine_dominant = 0.0;
    float sine_peak = 0.0F, gate_peak = 0.0F;
    std::size_t sine_cycles = 0, gate_pulses = 0;
    std::optional<std::size_t> gate_start, silent_start, empty_start;
    double gate_rms = 0.0, empty_rms = 1.0, silent_dominant = 1.0;
    const auto count = blokkily::test::count_allocations([&] {
        sine_rms = rms(sine);
        sine_peak = peak(sine);
        sine_cycles = rising_edges(sine);
        sine_on = goertzel_energy(sine, rate, 440.0);
        sine_off = goertzel_energy(sine, rate, 1000.0);
        sine_dominant = dominant_frequency(sine, rate, 100.0, 2000.0, 10.0);
        gate_peak = peak(gate);
        gate_pulses = rising_edges(gate, 0.1F);
        gate_start = first_nonzero(gate);
        gate_rms = rms(gate);
        silent_start = first_nonzero(silence);
        silent_dominant = dominant_frequency(silence, rate, 100.0, 2000.0, 10.0);
        empty_rms = rms({});
        empty_start = first_nonzero({});
    });
    blokkily::realtime::require_no_allocations(count, "the audio probes");

    require(std::abs(sine_rms - 0.5 / std::numbers::sqrt2) < 1e-4, "rms of a sine is A/sqrt(2)");
    require(std::abs(sine_peak - 0.5F) < 1e-3F, "peak of a sine is its amplitude");
    require(sine_cycles == 44, "a sine rises through zero once per cycle");
    require(std::abs(sine_on - 0.25) < 1e-3, "Goertzel energy at the tone is A squared");
    require(sine_off < 1e-6, "Goertzel energy away from the tone is near zero");
    require(sine_dominant == 440.0, "the dominant frequency of a 440 Hz sine is 440 Hz");
    require(gate_peak == 0.25F && gate_pulses == 3, "a gate rises once per pulse");
    require(gate_start == std::optional<std::size_t>{37}, "sound starts at the first pulse");
    require(std::abs(gate_rms - 0.25 * std::sqrt(150.0 / 1000.0)) < 1e-6, "rms of a gate");
    require(!silent_start && silent_dominant == 0.0, "silence has no start and no pitch");
    require(empty_rms == 0.0 && !empty_start, "an empty block is silent");
    // A signal already sounding at sample zero counts as one rise.
    const std::vector<float> held(16, 0.25F);
    require(rising_edges(held, 0.1F) == 1 && first_nonzero(held) == std::size_t{0},
            "a note sounding from sample zero is one rise starting at zero");
}
