// A stereo-linked feed-forward peak compressor with a hard knee, after
// Giannoulis, Massberg and Reiss (2012): the gain computer works in decibels
// on the louder channel, and the gain reduction is followed by a smooth
// decoupled peak detector, so a steady tone settles on the reduction its
// peaks call for. The coefficients are recomputed only when a parameter
// moves; per sample it takes one log10 and one exp.

#include "builtin_effect.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace blokkily::effects {
namespace {

constexpr std::array<ParameterSpec, 5> specs{{
    {"Threshold", -60.0, 0.0, -20.0},
    {"Ratio", 1.0, 20.0, 4.0},
    {"Attack", 0.1, 200.0, 10.0},
    {"Release", 5.0, 2000.0, 100.0},
    {"Makeup", 0.0, 24.0, 0.0},
}};

double smoothing(double milliseconds, double rate) noexcept {
    return std::exp(-1.0 / (milliseconds / 1000.0 * rate));
}

class Compressor final : public BuiltinEffect {
public:
    Compressor() : BuiltinEffect("compressor", specs) {}

    std::uint64_t tail_samples() const noexcept override { return 0; }

protected:
    void clear() noexcept override {
        release_state_ = 0.0;
        reduction_ = 0.0;
    }

    void prepare(double) override {
        release_state_ = 0.0;
        reduction_ = 0.0;
    }

    void update() noexcept override {
        const double rate = sample_rate();
        threshold_db_ = value(compressor::threshold_db);
        slope_ = 1.0 - 1.0 / value(compressor::ratio);
        attack_ = smoothing(value(compressor::attack_ms), rate);
        release_ = smoothing(value(compressor::release_ms), rate);
        makeup_db_ = value(compressor::makeup_db);
    }

    void render(float* left, float* right, std::size_t frames) noexcept override {
        constexpr double db_to_ln = std::numbers::ln10 / 20.0;
        const bool has_sc = sidechain_.left.size() >= frames && sidechain_.right.size() >= frames;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const double level = has_sc
                ? std::max(std::abs(sidechain_.left[frame]), std::abs(sidechain_.right[frame]))
                : std::max(std::abs(left[frame]), std::abs(right[frame]));
            const double level_db = level > 1e-9 ? 20.0 * std::log10(level) : -180.0;
            const double target = std::max(0.0, level_db - threshold_db_) * slope_;
            release_state_ = std::max(target, release_ * release_state_ + (1.0 - release_) * target);
            reduction_ = attack_ * reduction_ + (1.0 - attack_) * release_state_;
            const auto gain = static_cast<float>(std::exp((makeup_db_ - reduction_) * db_to_ln));
            left[frame] *= gain;
            right[frame] *= gain;
        }
    }

private:
    double threshold_db_ = -20.0;
    double slope_ = 0.75;
    double attack_ = 0.0;
    double release_ = 0.0;
    double makeup_db_ = 0.0;
    double release_state_ = 0.0;
    double reduction_ = 0.0;
};

} // namespace

std::unique_ptr<PluginInstance> make_compressor() { return std::make_unique<Compressor>(); }

} // namespace blokkily::effects
