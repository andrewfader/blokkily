// Three-band EQ: a low shelf, a peaking mid band and a high shelf, each an
// RBJ-cookbook biquad in transposed direct form II, in double precision.
// Coefficients are recomputed on the audio thread only when a parameter
// moves, with sin, cos, exp and sqrt: no allocation.

#include "builtin_effect.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace blokkily::effects {
namespace {

constexpr std::array<ParameterSpec, 7> specs{{
    {"Low Gain", -24.0, 24.0, 0.0},
    {"Low Freq", 20.0, 1000.0, 200.0},
    {"Mid Gain", -24.0, 24.0, 0.0},
    {"Mid Freq", 100.0, 10000.0, 1000.0},
    {"Mid Q", 0.1, 10.0, 0.707},
    {"High Gain", -24.0, 24.0, 0.0},
    {"High Freq", 1000.0, 20000.0, 5000.0},
}};

struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

struct BiquadState {
    double z1 = 0.0, z2 = 0.0;
    double run(const Biquad& f, double x) noexcept {
        const double y = f.b0 * x + z1;
        z1 = f.b1 * x - f.a1 * y + z2;
        z2 = f.b2 * x - f.a2 * y;
        return y;
    }
};

Biquad normalised(double b0, double b1, double b2, double a0, double a1, double a2) noexcept {
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

// 10^(gain_db / 40): the square root of the linear gain, as RBJ's A.
double shelf_a(double gain_db) noexcept { return std::exp(gain_db * std::numbers::ln10 / 40.0); }

double omega(double hz, double rate) noexcept {
    return 2.0 * std::numbers::pi * std::min(hz, 0.45 * rate) / rate;
}

Biquad low_shelf(double hz, double gain_db, double rate) noexcept {
    const double a = shelf_a(gain_db);
    const double w = omega(hz, rate);
    const double c = std::cos(w);
    const double alpha = std::sin(w) / 2.0 * std::numbers::sqrt2; // slope 1
    const double k = 2.0 * std::sqrt(a) * alpha;
    return normalised(a * ((a + 1) - (a - 1) * c + k), 2 * a * ((a - 1) - (a + 1) * c),
                      a * ((a + 1) - (a - 1) * c - k), (a + 1) + (a - 1) * c + k,
                      -2 * ((a - 1) + (a + 1) * c), (a + 1) + (a - 1) * c - k);
}

Biquad high_shelf(double hz, double gain_db, double rate) noexcept {
    const double a = shelf_a(gain_db);
    const double w = omega(hz, rate);
    const double c = std::cos(w);
    const double alpha = std::sin(w) / 2.0 * std::numbers::sqrt2;
    const double k = 2.0 * std::sqrt(a) * alpha;
    return normalised(a * ((a + 1) + (a - 1) * c + k), -2 * a * ((a - 1) + (a + 1) * c),
                      a * ((a + 1) + (a - 1) * c - k), (a + 1) - (a - 1) * c + k,
                      2 * ((a - 1) - (a + 1) * c), (a + 1) - (a - 1) * c - k);
}

Biquad peaking(double hz, double gain_db, double q, double rate) noexcept {
    const double a = shelf_a(gain_db);
    const double w = omega(hz, rate);
    const double c = std::cos(w);
    const double alpha = std::sin(w) / (2.0 * q);
    return normalised(1 + alpha * a, -2 * c, 1 - alpha * a, 1 + alpha / a, -2 * c, 1 - alpha / a);
}

class Eq3 final : public BuiltinEffect {
public:
    Eq3() : BuiltinEffect("eq3", specs) {}

protected:
    void prepare(double) override { state_ = {}; }
    void clear() noexcept override { state_ = {}; }

    void update() noexcept override {
        const double rate = sample_rate();
        bands_[0] = low_shelf(value(eq3::low_hz), value(eq3::low_gain_db), rate);
        bands_[1] = peaking(value(eq3::mid_hz), value(eq3::mid_gain_db), value(eq3::mid_q), rate);
        bands_[2] = high_shelf(value(eq3::high_hz), value(eq3::high_gain_db), rate);
    }

    void render(float* left, float* right, std::size_t frames) noexcept override {
        float* channels[2] = {left, right};
        for (std::size_t channel = 0; channel < 2; ++channel) {
            auto& state = state_[channel];
            float* samples = channels[channel];
            for (std::size_t frame = 0; frame < frames; ++frame) {
                double x = samples[frame];
                for (std::size_t band = 0; band < 3; ++band) x = state[band].run(bands_[band], x);
                samples[frame] = static_cast<float>(x);
            }
        }
    }

private:
    std::array<Biquad, 3> bands_{};
    std::array<std::array<BiquadState, 3>, 2> state_{};
};

} // namespace

std::unique_ptr<PluginInstance> make_eq3() { return std::make_unique<Eq3>(); }

} // namespace blokkily::effects
