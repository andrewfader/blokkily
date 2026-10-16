// Chorus and flanger: one design with two sets of defaults. The signal is
// mixed with a copy of itself read from a delay line whose length a sine LFO
// sweeps, the right channel's LFO a quarter turn behind the left's, so the
// copy drifts in pitch and time and the sound widens. A chorus uses a longer
// delay and no feedback; a flanger a delay of a millisecond or two with
// feedback, so the copy's comb filter sweeps audibly. Reads between samples
// are cubic (Hermite) interpolated. The LFO's phase is kept as a double and
// advanced per sample, so it is the same in any block size.

#include "builtin_effect.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace blokkily::effects {
namespace {

constexpr std::array<ParameterSpec, 5> chorus_specs{{
    {"Rate", 0.01, 10.0, 0.8},
    {"Depth", 0.0, 10.0, 3.0},
    {"Delay", 0.5, 30.0, 12.0},
    {"Feedback", -0.95, 0.95, 0.0},
    {"Mix", 0.0, 1.0, 0.5},
}};

constexpr std::array<ParameterSpec, 5> flanger_specs{{
    {"Rate", 0.01, 10.0, 0.25},
    {"Depth", 0.0, 10.0, 1.5},
    {"Delay", 0.5, 30.0, 2.0},
    {"Feedback", -0.95, 0.95, 0.6},
    {"Mix", 0.0, 1.0, 0.5},
}};

// The longest the line has to reach: the longest delay plus the deepest
// sweep, and a few samples for the interpolation.
constexpr double longest_ms = 41.0;

class Chorus final : public BuiltinEffect {
public:
    Chorus(std::string identifier, std::span<const ParameterSpec> specs)
        : BuiltinEffect(std::move(identifier), specs) {}

    std::uint64_t tail_samples() const noexcept override {
        const double reach = (base(chorus::delay_ms) + base(chorus::depth_ms)) / 1000.0 * sample_rate();
        const double feedback = std::abs(base(chorus::feedback));
        // Until feedback has let the copy fall 80 dB.
        const double passes = feedback > 0.0 ? std::ceil(std::log(1e-4) / std::log(feedback)) : 1.0;
        return static_cast<std::uint64_t>(std::ceil(reach * std::max(1.0, passes)));
    }

protected:
    void prepare(double rate) override {
        const auto samples = static_cast<std::size_t>(std::ceil(longest_ms / 1000.0 * rate)) + 4;
        for (auto& line : lines_) line.assign(samples, 0.0F);
        clear();
    }

    void clear() noexcept override {
        for (auto& line : lines_) std::fill(line.begin(), line.end(), 0.0F);
        written_ = 0;
        phase_ = 0.0;
    }

    void update() noexcept override {
        const double rate = sample_rate();
        step_ = 2.0 * std::numbers::pi * value(chorus::rate_hz) / rate;
        const double delay = value(chorus::delay_ms) / 1000.0 * rate;
        // The sweep never comes nearer than three samples, so the four the
        // interpolation reads are all written.
        depth_ = std::max(0.0, std::min(value(chorus::depth_ms) / 1000.0 * rate, delay - 3.0));
        centre_ = delay;
        feedback_ = value(chorus::feedback);
        wet_ = value(chorus::mix);
    }

    void render(float* left, float* right, std::size_t frames) noexcept override {
        const std::size_t size = lines_[0].size();
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const std::array<double, 2> dry{left[frame], right[frame]};
            std::array<double, 2> copy{};
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const double offset = channel == 0 ? 0.0 : std::numbers::pi / 2.0;
                const double delay = std::max(3.0, centre_ + depth_ * std::sin(phase_ + offset));
                copy[channel] = read(lines_[channel], static_cast<double>(written_) - delay);
                lines_[channel][written_ % size] =
                    static_cast<float>(dry[channel] + feedback_ * copy[channel]);
            }
            ++written_;
            phase_ += step_;
            if (phase_ > 2.0 * std::numbers::pi) phase_ -= 2.0 * std::numbers::pi;
            left[frame] = static_cast<float>(dry[0] + wet_ * (copy[0] - dry[0]));
            right[frame] = static_cast<float>(dry[1] + wet_ * (copy[1] - dry[1]));
        }
    }

private:
    // The line at a fractional sample position (absolute, in samples
    // written), Hermite-interpolated.
    [[nodiscard]] double read(const std::vector<float>& line, double at) const noexcept {
        const std::size_t size = line.size();
        const double base = std::floor(at);
        const double t = at - base;
        const auto index = static_cast<std::int64_t>(base);
        const auto sample = [&](std::int64_t i) -> double {
            if (i < 0) return 0.0;
            return line[static_cast<std::size_t>(i) % size];
        };
        const double y0 = sample(index - 1), y1 = sample(index), y2 = sample(index + 1),
                     y3 = sample(index + 2);
        const double c1 = 0.5 * (y2 - y0);
        const double c2 = y0 - 2.5 * y1 + 2.0 * y2 - 0.5 * y3;
        const double c3 = 0.5 * (y3 - y0) + 1.5 * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    std::array<std::vector<float>, 2> lines_;
    std::uint64_t written_ = 0;
    double phase_ = 0.0, step_ = 0.0, depth_ = 0.0, centre_ = 1.0, feedback_ = 0.0, wet_ = 0.5;
};

} // namespace

std::unique_ptr<PluginInstance> make_chorus() {
    return std::make_unique<Chorus>("chorus", chorus_specs);
}
std::unique_ptr<PluginInstance> make_flanger() {
    return std::make_unique<Chorus>("flanger", flanger_specs);
}

} // namespace blokkily::effects
