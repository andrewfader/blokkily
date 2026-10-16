// A stereo-linked lookahead true-peak limiter, for the master bus. The
// detector reads the 4x-oversampled signal (the windowed-sinc interpolation
// BS.1770-4 annex 2 measures with), so a peak between two samples is held
// under the ceiling as well as the samples themselves. The gain each point
// needs is held at its minimum across the lookahead and then averaged over
// it, so the gain has fully come down by the time the peak, delayed by the
// lookahead, reaches it; the average can only be at or under that minimum,
// so no peak passes. Release is a one-pole return towards unity. Below the
// ceiling the gain is exactly 1: the signal passes untouched, only delayed.
//
// The delay is reported as latency_samples(), so the engine's plugin delay
// compensation keeps the master in time with whatever else it is heard with,
// and an export starts where the song does.

#include "builtin_effect.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace blokkily::effects {
namespace {

constexpr std::array<ParameterSpec, 3> specs{{
    {"Ceiling", -12.0, 0.0, -1.0},
    {"Release", 1.0, 1000.0, 60.0},
    {"Gain", 0.0, 24.0, 0.0},
}};

// The interpolation kernel: 16 taps per phase, phases 1-3 between a sample
// and the next. The point a detector reading ends on is `half` samples old.
constexpr std::size_t taps = 16;
constexpr std::size_t half = taps / 2;
constexpr double lookahead_seconds = 0.0015;

std::array<std::array<double, taps>, 4> make_kernel() {
    std::array<std::array<double, taps>, 4> kernel{};
    for (std::size_t phase = 1; phase < 4; ++phase) {
        const double offset = static_cast<double>(phase) / 4.0;
        double sum = 0.0;
        for (std::size_t tap = 0; tap < taps; ++tap) {
            const double t = offset - (static_cast<double>(tap) - (static_cast<double>(half) - 1.0));
            const double sinc = std::sin(std::numbers::pi * t) / (std::numbers::pi * t);
            const double x = t / static_cast<double>(half);
            const double window = 0.42 + 0.5 * std::cos(std::numbers::pi * x) +
                                  0.08 * std::cos(2.0 * std::numbers::pi * x);
            kernel[phase][tap] = sinc * window;
            sum += kernel[phase][tap];
        }
        for (auto& weight : kernel[phase]) weight /= sum;
    }
    return kernel;
}

class Limiter final : public BuiltinEffect {
public:
    Limiter() : BuiltinEffect("limiter", specs), kernel_(make_kernel()) {}

    std::uint64_t tail_samples() const noexcept override { return 0; }
    std::uint32_t latency_samples() const noexcept override {
        return static_cast<std::uint32_t>(half + lookahead_);
    }

protected:
    void prepare(double rate) override {
        lookahead_ = std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(lookahead_seconds * rate)));
        delay_left_.assign(half + lookahead_ + 1, 0.0F);
        delay_right_.assign(delay_left_.size(), 0.0F);
        // The minimum spans the lookahead and one point more, so the two
        // interpolations either side of a sample are both covered.
        need_.assign(lookahead_ + 2, 1.0);
        held_.assign(lookahead_, 1.0);
        clear();
    }

    void clear() noexcept override {
        history_left_.fill(0.0);
        history_right_.fill(0.0);
        std::fill(delay_left_.begin(), delay_left_.end(), 0.0F);
        std::fill(delay_right_.begin(), delay_right_.end(), 0.0F);
        std::fill(need_.begin(), need_.end(), 1.0);
        std::fill(held_.begin(), held_.end(), 1.0);
        history_at_ = delay_at_ = need_at_ = held_at_ = 0;
        gain_ = 1.0;
    }

    void update() noexcept override {
        ceiling_ = std::pow(10.0, value(limiter::ceiling_dbtp) / 20.0);
        release_ = std::exp(-1.0 / (value(limiter::release_ms) / 1000.0 * sample_rate()));
        input_gain_ = std::pow(10.0, value(limiter::gain_db) / 20.0);
    }

    void render(float* left, float* right, std::size_t frames) noexcept override {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const double in_left = left[frame] * input_gain_;
            const double in_right = right[frame] * input_gain_;
            history_left_[history_at_] = in_left;
            history_right_[history_at_] = in_right;
            history_at_ = (history_at_ + 1) % taps;
            // The loudest the signal comes around the sample `half` old.
            const double peak = std::max(peak_of(history_left_), peak_of(history_right_));
            const double need = peak > ceiling_ ? ceiling_ / peak : 1.0;
            need_[need_at_] = need;
            need_at_ = (need_at_ + 1) % need_.size();
            double held = 1.0;
            for (const double value : need_) held = std::min(held, value);
            // Averaged over the lookahead: summed exactly each time, so no
            // rounding drifts in over a long song.
            held_[held_at_] = held;
            held_at_ = (held_at_ + 1) % held_.size();
            double sum = 0.0;
            for (const double value : held_) sum += value;
            const double smooth = sum / static_cast<double>(held_.size());
            gain_ = std::min(smooth, 1.0 - (1.0 - gain_) * release_);
            // The signal as it was `half + lookahead` samples ago.
            delay_left_[delay_at_] = static_cast<float>(in_left);
            delay_right_[delay_at_] = static_cast<float>(in_right);
            delay_at_ = (delay_at_ + 1) % delay_left_.size();
            const auto oldest = delay_at_;
            const auto gain = static_cast<float>(gain_);
            left[frame] = gain_ >= 1.0 ? delay_left_[oldest] : delay_left_[oldest] * gain;
            right[frame] = gain_ >= 1.0 ? delay_right_[oldest] : delay_right_[oldest] * gain;
        }
    }

private:
    // The true peak around the sample `half` old in a channel's history.
    [[nodiscard]] double peak_of(const std::array<double, taps>& history) const noexcept {
        // history_at_ is the oldest sample now.
        std::array<double, taps> window{};
        for (std::size_t tap = 0; tap < taps; ++tap) window[tap] = history[(history_at_ + tap) % taps];
        double peak = std::max(std::abs(window[half - 1]), std::abs(window[half]));
        for (std::size_t phase = 1; phase < 4; ++phase) {
            double value = 0.0;
            for (std::size_t tap = 0; tap < taps; ++tap) value += kernel_[phase][tap] * window[tap];
            peak = std::max(peak, std::abs(value));
        }
        return peak;
    }

    const std::array<std::array<double, taps>, 4> kernel_;
    std::size_t lookahead_ = 72;
    std::array<double, taps> history_left_{};
    std::array<double, taps> history_right_{};
    std::vector<float> delay_left_, delay_right_;
    std::vector<double> need_, held_;
    std::size_t history_at_ = 0, delay_at_ = 0, need_at_ = 0, held_at_ = 0;
    double ceiling_ = 0.891, release_ = 0.999, input_gain_ = 1.0, gain_ = 1.0;
};

} // namespace

std::unique_ptr<PluginInstance> make_limiter() { return std::make_unique<Limiter>(); }

} // namespace blokkily::effects
