#pragma once

#include "blokkily/model/processor_address.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace blokkily {

enum class LfoWaveform : std::uint8_t {
    sine,
    triangle,
    saw_up,
    saw_down,
    square,
    sample_and_hold
};

struct LfoConfig {
    LfoWaveform waveform = LfoWaveform::sine;
    double frequency_hz = 1.0;
    double tempo_sync_beats = 0.0; // 0.0 = free-running, > 0.0 = synced to beat fractions (e.g. 1.0 = 1 beat, 4.0 = 1 bar)
    float depth = 1.0F;            // 0.0 to 1.0
    bool bipolar = true;           // true = [-depth..+depth], false = [0..depth]
    float phase_offset = 0.0F;     // 0.0 to 1.0
};

class LfoModulator {
public:
    explicit LfoModulator(LfoConfig config = {});

    void set_config(const LfoConfig& config) noexcept;
    [[nodiscard]] const LfoConfig& config() const noexcept { return config_; }

    // Advances phase for the given number of frames and returns the output modulation value
    float process(std::size_t frames, double sample_rate, double bpm = 120.0) noexcept;
    [[nodiscard]] float current_value() const noexcept { return current_value_; }
    void reset(float phase = 0.0F) noexcept;

private:
    LfoConfig config_;
    double phase_ = 0.0;
    float current_value_ = 0.0F;
    float last_sh_val_ = 0.0F;
    std::uint32_t rng_state_ = 0x12345678;
};

struct EnvelopeFollowerConfig {
    float attack_ms = 10.0F;
    float release_ms = 100.0F;
    float gain_db = 0.0F;
    float min_out = 0.0F;
    float max_out = 1.0F;
};

class EnvelopeFollowerModulator {
public:
    explicit EnvelopeFollowerModulator(EnvelopeFollowerConfig config = {});

    void set_config(const EnvelopeFollowerConfig& config) noexcept;
    [[nodiscard]] const EnvelopeFollowerConfig& config() const noexcept { return config_; }

    // Evaluates audio signal in block and computes smoothed envelope
    float process(StereoBlock audio, double sample_rate) noexcept;
    [[nodiscard]] float current_value() const noexcept { return current_value_; }
    void reset() noexcept;

private:
    EnvelopeFollowerConfig config_;
    float envelope_ = 0.0F;
    float current_value_ = 0.0F;
};

struct MacroTarget {
    ProcessorAddress where;
    std::int32_t parameter_id = 0;
    float min_value = 0.0F;
    float max_value = 1.0F;
};

class MacroModulator {
public:
    explicit MacroModulator(std::string name = "Macro 1", float initial_value = 0.0F);

    MacroModulator(MacroModulator&& other) noexcept
        : name_(std::move(other.name_)),
          value_(other.value_.load(std::memory_order_relaxed)),
          targets_(std::move(other.targets_)) {}

    MacroModulator& operator=(MacroModulator&& other) noexcept {
        if (this != &other) {
            name_ = std::move(other.name_);
            value_.store(other.value_.load(std::memory_order_relaxed), std::memory_order_relaxed);
            targets_ = std::move(other.targets_);
        }
        return *this;
    }

    void set_value(float value) noexcept { value_.store(std::clamp(value, 0.0F, 1.0F), std::memory_order_relaxed); }
    [[nodiscard]] float value() const noexcept { return value_.load(std::memory_order_relaxed); }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }

    void add_target(MacroTarget target);
    void clear_targets();
    [[nodiscard]] std::span<const MacroTarget> targets() const noexcept { return targets_; }

private:
    std::string name_;
    std::atomic<float> value_{0.0F};
    std::vector<MacroTarget> targets_;
};

// Target destination for an LFO or Envelope Follower
struct ModulationRoute {
    ProcessorAddress where;
    std::int32_t parameter_id = 0;
    float amount = 1.0F; // multiplier applied to modulator output [-1..1]
};

// Central modulation coordinator evaluating active modulators and dispatching PluginEvents
class ModulationMatrix {
public:
    ModulationMatrix();
    ~ModulationMatrix() = default;

    ModulationMatrix(const ModulationMatrix&) = delete;
    ModulationMatrix& operator=(const ModulationMatrix&) = delete;

    std::size_t add_lfo(LfoConfig config = {});
    std::size_t add_envelope_follower(EnvelopeFollowerConfig config = {});
    std::size_t add_macro(std::string name = "Macro", float initial_value = 0.0F);

    void route_lfo(std::size_t lfo_index, ModulationRoute route);
    void route_envelope_follower(std::size_t env_index, ModulationRoute route);

    [[nodiscard]] LfoModulator& lfo(std::size_t index) { return lfos_.at(index); }
    [[nodiscard]] EnvelopeFollowerModulator& envelope_follower(std::size_t index) { return envs_.at(index); }
    [[nodiscard]] MacroModulator& macro(std::size_t index) { return macros_.at(index); }

    [[nodiscard]] std::size_t lfo_count() const noexcept { return lfos_.size(); }
    [[nodiscard]] std::size_t envelope_follower_count() const noexcept { return envs_.size(); }
    [[nodiscard]] std::size_t macro_count() const noexcept { return macros_.size(); }

    // Evaluates all modulators for the chunk (audio thread)
    void process(StereoBlock audio, double sample_rate, double bpm, std::size_t frames) noexcept;

    // Dispatches parameter_modulation events for `where` into out_events (audio thread, noalloc)
    // Returns number of events written
    std::size_t generate_events_for(ProcessorAddress where,
                                    std::span<PluginEvent> out_events,
                                    std::uint32_t sample_offset = 0) const noexcept;

private:
    std::vector<LfoModulator> lfos_;
    std::vector<std::vector<ModulationRoute>> lfo_routes_;

    std::vector<EnvelopeFollowerModulator> envs_;
    std::vector<std::vector<ModulationRoute>> env_routes_;

    std::vector<MacroModulator> macros_;
};

} // namespace blokkily
