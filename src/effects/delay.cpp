// A feedback echo per channel. The echo time is a whole number of samples,
// either from milliseconds or, synced, from beats at the tempo the transport
// last reported, and is clamped to the four seconds the line holds.
//
// out = (1 - mix) * in + mix * echo, and each echo feeds back at `feedback`,
// so an impulse comes back at t, 2t, 3t ... with amplitudes mix, mix*fb, ...

#include "blokkily/effects/delay_line.hpp"
#include "builtin_effect.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace blokkily::effects {
namespace {

constexpr std::array<ParameterSpec, 5> specs{{
    {"Time", 1.0, 2000.0, 250.0},
    {"Feedback", 0.0, 0.95, 0.5},
    {"Mix", 0.0, 1.0, 0.5},
    {"Sync", 0.0, 1.0, 0.0},
    {"Beats", 0.0625, 4.0, 0.5},
}};

constexpr double line_seconds = 4.0;

class Delay final : public BuiltinEffect {
public:
    Delay() : BuiltinEffect("delay", specs) {}

    std::uint64_t tail_samples() const noexcept override {
        const double feedback = base(delay::feedback);
        const auto echo = static_cast<double>(delay_samples(
            base(delay::sync) >= 0.5, base(delay::time_ms), base(delay::beats), bpm(),
            sample_rate()));
        // Until the echoes have fallen 80 dB below the sound that made them.
        const double echoes =
            feedback > 0.0 ? std::ceil(std::log(1e-4) / std::log(feedback)) : 1.0;
        return static_cast<std::uint64_t>(echo * std::max(1.0, echoes));
    }

protected:
    void prepare(double rate) override {
        const auto samples = static_cast<std::size_t>(std::ceil(line_seconds * rate));
        for (auto& line : lines_) line.resize(samples);
    }

    void update() noexcept override {
        delay_ = delay_samples(value(delay::sync) >= 0.5, value(delay::time_ms),
                               value(delay::beats), bpm(), sample_rate());
        delay_ = std::clamp<std::size_t>(delay_, 1, lines_[0].max_delay());
        feedback_ = static_cast<float>(value(delay::feedback));
        wet_ = static_cast<float>(value(delay::mix));
        dry_ = 1.0F - wet_;
    }

    void render(float* left, float* right, std::size_t frames) noexcept override {
        float* channels[2] = {left, right};
        for (std::size_t channel = 0; channel < 2; ++channel) {
            auto& line = lines_[channel];
            float* samples = channels[channel];
            for (std::size_t frame = 0; frame < frames; ++frame) {
                const float input = samples[frame];
                const float echo = line.read(delay_);
                line.write(input + feedback_ * echo);
                samples[frame] = dry_ * input + wet_ * echo;
            }
        }
    }

private:
    static std::size_t delay_samples(bool synced, double ms, double beats, double bpm,
                                     double rate) noexcept {
        const double seconds = synced ? beats * 60.0 / bpm : ms / 1000.0;
        return static_cast<std::size_t>(std::llround(std::min(seconds, line_seconds) * rate));
    }

    std::array<DelayLine, 2> lines_;
    std::size_t delay_ = 1;
    float feedback_ = 0.0F;
    float wet_ = 0.0F;
    float dry_ = 1.0F;
};

} // namespace

std::unique_ptr<PluginInstance> make_delay() { return std::make_unique<Delay>(); }

} // namespace blokkily::effects
