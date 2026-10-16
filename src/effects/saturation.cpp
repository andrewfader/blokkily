// Soft saturation: the signal is driven into a tanh curve, which rounds its
// peaks off and adds odd harmonics, warmer the harder it is pushed. The curve
// is applied with first-order antiderivative anti-aliasing (Parker, Zavalishin
// and Le Bivic, DAFx 2016): instead of tanh(x) of each sample, the average of
// tanh over the line between this sample and the last, from tanh's
// antiderivative log(cosh(x)). That keeps the harmonics a hard drive makes
// above Nyquist from folding back down as inharmonic noise, without
// oversampling. It costs half a sample of delay, below what is reported.

#include "builtin_effect.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace blokkily::effects {
namespace {

constexpr std::array<ParameterSpec, 3> specs{{
    {"Drive", 0.0, 36.0, 12.0},
    {"Mix", 0.0, 1.0, 1.0},
    {"Output", -24.0, 6.0, -6.0},
}};

// log(cosh(x)), without overflow for large |x|.
double log_cosh(double x) noexcept {
    const double a = std::abs(x);
    return a > 20.0 ? a - std::log(2.0) : std::log(std::cosh(a));
}

class Saturation final : public BuiltinEffect {
public:
    Saturation() : BuiltinEffect("saturation", specs) {}

    std::uint64_t tail_samples() const noexcept override { return 0; }

protected:
    void prepare(double) override { clear(); }
    void clear() noexcept override { last_ = {}; }

    void update() noexcept override {
        drive_ = std::pow(10.0, value(saturation::drive_db) / 20.0);
        wet_ = value(saturation::mix);
        output_ = std::pow(10.0, value(saturation::output_db) / 20.0);
    }

    void render(float* left, float* right, std::size_t frames) noexcept override {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            left[frame] = static_cast<float>(shape(last_[0], left[frame]));
            right[frame] = static_cast<float>(shape(last_[1], right[frame]));
        }
    }

private:
    struct Last {
        double driven = 0.0, antiderivative = 0.0;
    };

    double shape(Last& last, double input) noexcept {
        const double x = input * drive_;
        const double f = log_cosh(x);
        const double dx = x - last.driven;
        const double curved = std::abs(dx) > 1e-5 ? (f - last.antiderivative) / dx
                                                  : std::tanh(0.5 * (x + last.driven));
        last = {x, f};
        const double wet = curved * output_;
        return input + wet_ * (wet - input);
    }

    std::array<Last, 2> last_{};
    double drive_ = 4.0, wet_ = 1.0, output_ = 0.5;
};

} // namespace

std::unique_ptr<PluginInstance> make_saturation() { return std::make_unique<Saturation>(); }

} // namespace blokkily::effects
