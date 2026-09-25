// features/builtin_effects.feature: Built-in effects keep the real-time rules.
//
// Every built-in effect, and both effect fixtures through the production CLAP
// and VST3 adapters, run a thousand blocks of a steady tone with the
// audio-thread calls the engine will make: set_transport(), process() with
// parameter values and modulation at sample offsets (so coefficients and
// delay times are recomputed on the audio thread), and take_parameter_edits().
// Not one armed call may allocate or free, and the rendered audio proves each
// effect really processed.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/effects/builtin.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <array>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

namespace {

using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;
constexpr double rate = 48000.0;

// Runs `effect` over a 440 Hz tone and returns the rendered left channel.
std::vector<float> run(blokkily::PluginInstance& effect, const std::string& what,
                       std::int32_t moved_parameter, double low, double high) {
    using namespace blokkily;
    require(effect.activate(rate, 1, block), what + " must activate");
    std::vector<float> left(calls * block), right(calls * block);
    for (std::size_t frame = 0; frame < left.size(); ++frame)
        left[frame] = right[frame] = static_cast<float>(
            0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(frame) / rate));
    std::array<ParameterEdit, 16> edits{};
    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        // Every block moves a parameter mid-block and modulates it later on,
        // and every tenth block changes the tempo a synced delay follows.
        const std::array<PluginEvent, 2> events{
            PluginEvent{PluginEvent::Type::parameter_value, 37, moved_parameter,
                        call % 2 == 0 ? low : high},
            PluginEvent{PluginEvent::Type::parameter_modulation, 181, moved_parameter,
                        call % 3 == 0 ? 0.0 : (high - low) * 0.1}};
        const TransportInfo transport{call % 20 < 10 ? 120.0 : 132.0,
                                      static_cast<double>(call * block) / 24000.0, 0, 4, 4, true};
        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        {
            test::AllocationGuard guard;
            effect.set_transport(transport);
            effect.process({out_left, out_right}, events);
            (void)effect.take_parameter_edits(edits);
            (void)effect.tail_samples();
            total += guard.count();
        }
    }
    require_no_allocations(total, what.c_str());
    require(probe::rms(std::span<const float>(left).subspan(left.size() - 48000)) > 1e-3,
            what + " must still sound after a thousand blocks");
    return left;
}

} // namespace

BLOKKILY_REALTIME_CASE(builtin_effects) {
    using namespace blokkily;
    struct Move {
        const char* identifier;
        std::int32_t parameter;
        double low, high;
    };
    const std::array<Move, 5> moves{{{"eq3", eq3::mid_gain_db, -9.0, 9.0},
                                     {"delay", delay::time_ms, 50.0, 400.0},
                                     {"delay", delay::sync, 0.0, 1.0},
                                     {"reverb", reverb::size, 0.2, 0.9},
                                     {"compressor", compressor::threshold_db, -30.0, -10.0}}};
    for (const auto& move : moves) {
        auto effect = create_builtin_effect(move.identifier);
        require(effect != nullptr, std::string("built-in ") + move.identifier + " must exist");
        const std::string what = std::string("built-in ") + move.identifier;
        const auto rendered = run(*effect, what, move.parameter, move.low, move.high);
        // The tone was changed: the effect did not just pass it through.
        double difference = 0.0;
        for (std::size_t frame = 0; frame < rendered.size(); ++frame)
            difference = std::max(difference, std::abs(static_cast<double>(rendered[frame]) -
                0.5 * std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(frame) / rate)));
        require(difference > 1e-3, what + " must change the tone it processes");
    }

    std::string error;
    auto clap = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_EFFECT_PATH,
                                           "dev.blokkily.test.effect", &error);
    require(clap != nullptr, "the CLAP effect must load through the production adapter: " + error);
    (void)run(*clap, "CLAP effect", 0, 0.2, 0.8);

    auto vst3 = Vst3PluginInstance::create(BLOKKILY_TEST_VST3_EFFECT_PATH, 0, &error);
    require(vst3 != nullptr, "the VST3 effect must load through JUCE: " + error);
    (void)run(*vst3, "VST3 effect", 0, 0.2, 0.8);
}
