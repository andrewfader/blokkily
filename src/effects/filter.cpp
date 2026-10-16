// A resonant state-variable filter (the trapezoidal "TPT" SVF, after
// Zavalishin, The Art of VA Filter Design, ch. 4): low-pass, high-pass or
// band-pass, 12 dB per octave, with resonance up to a strong peak. The
// cutoff glides to a new value over a few milliseconds, so an LFO or an
// automation lane sweeps it without zipper noise. Per sample it takes one
// tan while the cutoff is moving, and none once it has arrived.

#include "builtin_effect.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace blokkily::effects {
namespace {

constexpr std::array<ParameterSpec, 4> specs{{
    {"Mode", 0.0, 2.0, 0.0},
    {"Cutoff", 20.0, 20000.0, 1000.0},
    {"Resonance", 0.0, 1.0, 0.1},
    {"Mix", 0.0, 1.0, 1.0},
}};

class Filter final : public BuiltinEffect {
public:
    Filter() : BuiltinEffect("filter", specs) {}

    std::uint64_t tail_samples() const noexcept override { return 0; }

protected:
    void prepare(double rate) override {
        // About 5 ms for the cutoff to glide most of the way to a new value.
        glide_ = std::exp(-1.0 / (0.005 * rate));
        clear();
    }

    void clear() noexcept override {
        state_ = {};
        // Until it has played a sample the filter has nothing to glide from:
        // the first cutoff it is given is where it starts.
        fresh_ = true;
    }

    void update() noexcept override {
        mode_ = static_cast<int>(std::lround(value(filter::mode)));
        target_ = std::clamp(value(filter::cutoff_hz), 20.0, 0.49 * sample_rate());
        // Resonance 0 is Q 0.5 (no peak), 1 is Q 25.
        const double resonance = value(filter::resonance);
        damping_ = 1.0 / (0.5 + resonance * resonance * 24.5);
        wet_ = value(filter::mix);
        if (fresh_) set_cutoff(target_);
    }

    void render(float* left, float* right, std::size_t frames) noexcept override {
        if (frames > 0) fresh_ = false;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            if (cutoff_ != target_) {
                // Glides in the log domain, so a sweep sounds even.
                const double next = std::exp(std::log(target_) + glide_ * (std::log(cutoff_) - std::log(target_)));
                set_cutoff(std::abs(next - target_) < 1e-6 * target_ ? target_ : next);
            }
            left[frame] = static_cast<float>(run(state_[0], left[frame]));
            right[frame] = static_cast<float>(run(state_[1], right[frame]));
        }
    }

private:
    struct State {
        double s1 = 0.0, s2 = 0.0;
    };

    void set_cutoff(double hz) noexcept {
        cutoff_ = hz;
        g_ = std::tan(std::numbers::pi * hz / sample_rate());
        h_ = 1.0 / (1.0 + damping_ * g_ + g_ * g_);
    }

    double run(State& state, double input) noexcept {
        // Damping may have moved since the cutoff did.
        h_ = 1.0 / (1.0 + damping_ * g_ + g_ * g_);
        const double high = (input - (damping_ + g_) * state.s1 - state.s2) * h_;
        const double band = g_ * high + state.s1;
        state.s1 = g_ * high + band;
        const double low = g_ * band + state.s2;
        state.s2 = g_ * band + low;
        // The band-pass is scaled to unity at its centre, whatever the Q.
        const double out = mode_ == 1 ? high : mode_ == 2 ? damping_ * band : low;
        return wet_ >= 1.0 ? out : input + wet_ * (out - input);
    }

    std::array<State, 2> state_{};
    int mode_ = 0;
    bool fresh_ = true;
    double target_ = 1000.0, cutoff_ = -1.0, glide_ = 0.99;
    double damping_ = 1.0, g_ = 0.0, h_ = 1.0, wet_ = 1.0;
};

} // namespace

std::unique_ptr<PluginInstance> make_filter() { return std::make_unique<Filter>(); }

} // namespace blokkily::effects
