#include "blokkily/audio/modulator.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace blokkily {

namespace {

constexpr double two_pi = 2.0 * std::numbers::pi;

} // namespace

LfoModulator::LfoModulator(LfoConfig config) : config_(config) {}

void LfoModulator::set_config(const LfoConfig& config) noexcept {
    config_ = config;
}

void LfoModulator::reset(float phase) noexcept {
    phase_ = std::fmod(static_cast<double>(phase), 1.0);
    if (phase_ < 0.0) phase_ += 1.0;
    current_value_ = 0.0F;
}

float LfoModulator::process(std::size_t frames, double sample_rate, double bpm) noexcept {
    if (sample_rate <= 0.0) return 0.0F;

    double freq = config_.frequency_hz;
    if (config_.tempo_sync_beats > 0.0 && bpm > 0.0) {
        freq = bpm / (60.0 * config_.tempo_sync_beats);
    }

    const double delta_phase = (freq * static_cast<double>(frames)) / sample_rate;
    const double old_phase = phase_;
    phase_ = std::fmod(phase_ + delta_phase, 1.0);

    const double p = std::fmod(phase_ + static_cast<double>(config_.phase_offset), 1.0);
    const double normalized_p = p < 0.0 ? p + 1.0 : p;

    float raw_val = 0.0F;
    switch (config_.waveform) {
    case LfoWaveform::sine:
        raw_val = static_cast<float>(std::sin(normalized_p * two_pi));
        break;
    case LfoWaveform::triangle:
        if (normalized_p < 0.25) {
            raw_val = static_cast<float>(4.0 * normalized_p);
        } else if (normalized_p < 0.75) {
            raw_val = static_cast<float>(2.0 - 4.0 * normalized_p);
        } else {
            raw_val = static_cast<float>(4.0 * normalized_p - 4.0);
        }
        break;
    case LfoWaveform::saw_up:
        raw_val = static_cast<float>(2.0 * normalized_p - 1.0);
        break;
    case LfoWaveform::saw_down:
        raw_val = static_cast<float>(1.0 - 2.0 * normalized_p);
        break;
    case LfoWaveform::square:
        raw_val = normalized_p < 0.5 ? 1.0F : -1.0F;
        break;
    case LfoWaveform::sample_and_hold:
        if (phase_ < old_phase) {
            rng_state_ = rng_state_ * 1664525u + 1013904223u;
            last_sh_val_ = static_cast<float>(rng_state_ & 0x7FFFFFFF) / 1073741824.0F - 1.0F;
        }
        raw_val = last_sh_val_;
        break;
    }

    if (config_.bipolar) {
        current_value_ = raw_val * config_.depth;
    } else {
        current_value_ = ((raw_val + 1.0F) * 0.5F) * config_.depth;
    }
    return current_value_;
}

EnvelopeFollowerModulator::EnvelopeFollowerModulator(EnvelopeFollowerConfig config)
    : config_(config) {}

void EnvelopeFollowerModulator::set_config(const EnvelopeFollowerConfig& config) noexcept {
    config_ = config;
}

void EnvelopeFollowerModulator::reset() noexcept {
    envelope_ = 0.0F;
    current_value_ = 0.0F;
}

float EnvelopeFollowerModulator::process(StereoBlock audio, double sample_rate) noexcept {
    const std::size_t frames = std::min(audio.left.size(), audio.right.size());
    if (frames == 0 || sample_rate <= 0.0) return current_value_;

    float peak = 0.0F;
    for (std::size_t i = 0; i < frames; ++i) {
        peak = std::max({peak, std::abs(audio.left[i]), std::abs(audio.right[i])});
    }

    const float lin_gain = std::pow(10.0F, config_.gain_db / 20.0F);
    const float target = peak * lin_gain;

    const float attack_sec = std::max(0.001F, config_.attack_ms / 1000.0F);
    const float release_sec = std::max(0.001F, config_.release_ms / 1000.0F);
    const float dt = static_cast<float>(frames) / static_cast<float>(sample_rate);

    const float alpha_a = std::exp(-dt / attack_sec);
    const float alpha_r = std::exp(-dt / release_sec);

    if (target > envelope_) {
        envelope_ = alpha_a * envelope_ + (1.0F - alpha_a) * target;
    } else {
        envelope_ = alpha_r * envelope_ + (1.0F - alpha_r) * target;
    }

    const float norm = std::clamp(envelope_, 0.0F, 1.0F);
    current_value_ = config_.min_out + norm * (config_.max_out - config_.min_out);
    return current_value_;
}

MacroModulator::MacroModulator(std::string name, float initial_value)
    : name_(std::move(name)), value_(std::clamp(initial_value, 0.0F, 1.0F)) {}

void MacroModulator::add_target(MacroTarget target) {
    targets_.push_back(target);
}

void MacroModulator::clear_targets() {
    targets_.clear();
}

ModulationMatrix::ModulationMatrix() = default;

std::size_t ModulationMatrix::add_lfo(LfoConfig config) {
    lfos_.emplace_back(config);
    lfo_routes_.emplace_back();
    return lfos_.size() - 1;
}

std::size_t ModulationMatrix::add_envelope_follower(EnvelopeFollowerConfig config) {
    envs_.emplace_back(config);
    env_routes_.emplace_back();
    return envs_.size() - 1;
}

std::size_t ModulationMatrix::add_macro(std::string name, float initial_value) {
    macros_.emplace_back(std::move(name), initial_value);
    return macros_.size() - 1;
}

void ModulationMatrix::route_lfo(std::size_t lfo_index, ModulationRoute route) {
    if (lfo_index < lfo_routes_.size()) {
        lfo_routes_[lfo_index].push_back(route);
    }
}

void ModulationMatrix::route_envelope_follower(std::size_t env_index, ModulationRoute route) {
    if (env_index < env_routes_.size()) {
        env_routes_[env_index].push_back(route);
    }
}

void ModulationMatrix::process(StereoBlock audio, double sample_rate, double bpm,
                               std::size_t frames) noexcept {
    for (auto& lfo : lfos_) {
        lfo.process(frames, sample_rate, bpm);
    }
    for (auto& env : envs_) {
        env.process(audio, sample_rate);
    }
}

std::size_t ModulationMatrix::generate_events_for(ProcessorAddress where,
                                                  std::span<PluginEvent> out_events,
                                                  std::uint32_t sample_offset) const noexcept {
    std::size_t count = 0;
    const std::size_t capacity = out_events.size();

    // 1. LFO Routes
    for (std::size_t i = 0; i < lfos_.size(); ++i) {
        const float val = lfos_[i].current_value();
        for (const auto& route : lfo_routes_[i]) {
            if (count >= capacity) return count;
            if (route.where == where) {
                out_events[count++] = PluginEvent{
                    .type = PluginEvent::Type::parameter_modulation,
                    .sample_offset = sample_offset,
                    .key_or_parameter = route.parameter_id,
                    .value = static_cast<double>(val * route.amount),
                    .cents = 0.0
                };
            }
        }
    }

    // 2. Envelope Follower Routes
    for (std::size_t i = 0; i < envs_.size(); ++i) {
        const float val = envs_[i].current_value();
        for (const auto& route : env_routes_[i]) {
            if (count >= capacity) return count;
            if (route.where == where) {
                out_events[count++] = PluginEvent{
                    .type = PluginEvent::Type::parameter_modulation,
                    .sample_offset = sample_offset,
                    .key_or_parameter = route.parameter_id,
                    .value = static_cast<double>(val * route.amount),
                    .cents = 0.0
                };
            }
        }
    }

    // 3. Macro Targets
    for (const auto& macro : macros_) {
        const float val = macro.value();
        for (const auto& target : macro.targets()) {
            if (count >= capacity) return count;
            if (target.where == where) {
                const float scaled = target.min_value + val * (target.max_value - target.min_value);
                out_events[count++] = PluginEvent{
                    .type = PluginEvent::Type::parameter_modulation,
                    .sample_offset = sample_offset,
                    .key_or_parameter = target.parameter_id,
                    .value = static_cast<double>(scaled),
                    .cents = 0.0
                };
            }
        }
    }

    return count;
}

} // namespace blokkily
