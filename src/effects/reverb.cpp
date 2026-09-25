// A Freeverb-style reverb (Jezar's public-domain design): per channel, eight
// damped feedback combs in parallel and four allpasses in series, the right
// channel's delays spread a little longer than the left's. The mono sum of
// the input feeds both. Delay lengths are the classic 44.1 kHz ones scaled to
// the sample rate, sized in prepare(); nothing allocates while processing.

#include "builtin_effect.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace blokkily::effects {
namespace {

constexpr std::array<ParameterSpec, 4> specs{{
    {"Size", 0.0, 1.0, 0.7},
    {"Damping", 0.0, 1.0, 0.5},
    {"Width", 0.0, 1.0, 1.0},
    {"Mix", 0.0, 1.0, 0.3},
}};

constexpr std::array<std::size_t, 8> comb_lengths{1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
constexpr std::array<std::size_t, 4> allpass_lengths{556, 441, 341, 225};
constexpr std::size_t stereo_spread = 23;
constexpr double reference_rate = 44100.0;
constexpr float input_gain = 0.015F;
constexpr float wet_scale = 3.0F;
constexpr float allpass_feedback = 0.5F;

// Anything this small is gone; keeping it would only drift into denormals.
float flushed(float value) noexcept { return std::abs(value) < 1e-25F ? 0.0F : value; }

struct Comb {
    std::vector<float> buffer;
    std::size_t index = 0;
    float store = 0.0F;
    float run(float input, float feedback, float damp) noexcept {
        const float output = buffer[index];
        store = flushed(output * (1.0F - damp) + store * damp);
        buffer[index] = input + store * feedback;
        if (++index == buffer.size()) index = 0;
        return output;
    }
};

struct Allpass {
    std::vector<float> buffer;
    std::size_t index = 0;
    float run(float input) noexcept {
        const float delayed = buffer[index];
        buffer[index] = flushed(input + delayed * allpass_feedback);
        if (++index == buffer.size()) index = 0;
        return delayed - input;
    }
};

std::size_t scaled(std::size_t length, double rate) {
    return std::max<std::size_t>(1, static_cast<std::size_t>(
                                        std::llround(static_cast<double>(length) * rate / reference_rate)));
}

class Reverb final : public BuiltinEffect {
public:
    Reverb() : BuiltinEffect("reverb", specs) {}

    std::uint64_t tail_samples() const noexcept override {
        // Each trip round a comb scales what it holds by at most `feedback`
        // (its damping only takes more), so the longest comb is 80 dB down
        // after `trips` trips; the allpasses, at feedback 0.5, ring on for
        // another fourteen trips each.
        const double trips =
            std::ceil(std::log(1e-4) / std::log(room_feedback(base(reverb::size))));
        double allpass_ring = 0.0;
        for (const auto length : allpass_lengths)
            allpass_ring += 14.0 * static_cast<double>(length + stereo_spread);
        const double longest_comb = static_cast<double>(comb_lengths.back() + stereo_spread);
        return static_cast<std::uint64_t>((trips * longest_comb + allpass_ring) * sample_rate() /
                                          reference_rate);
    }

protected:
    void prepare(double rate) override {
        for (std::size_t channel = 0; channel < 2; ++channel) {
            const std::size_t spread = channel == 0 ? 0 : stereo_spread;
            for (std::size_t index = 0; index < combs_[channel].size(); ++index) {
                auto& comb = combs_[channel][index];
                comb.buffer.assign(scaled(comb_lengths[index] + spread, rate), 0.0F);
                comb.index = 0;
                comb.store = 0.0F;
            }
            for (std::size_t index = 0; index < allpasses_[channel].size(); ++index) {
                auto& allpass = allpasses_[channel][index];
                allpass.buffer.assign(scaled(allpass_lengths[index] + spread, rate), 0.0F);
                allpass.index = 0;
            }
        }
    }

    void clear() noexcept override {
        for (auto& channel : combs_)
            for (auto& comb : channel) {
                std::fill(comb.buffer.begin(), comb.buffer.end(), 0.0F);
                comb.index = 0;
                comb.store = 0.0F;
            }
        for (auto& channel : allpasses_)
            for (auto& allpass : channel) {
                std::fill(allpass.buffer.begin(), allpass.buffer.end(), 0.0F);
                allpass.index = 0;
            }
    }

    void update() noexcept override {
        feedback_ = static_cast<float>(room_feedback(value(reverb::size)));
        damp_ = static_cast<float>(value(reverb::damping) * 0.4);
        const auto mix = static_cast<float>(value(reverb::mix));
        const auto width = static_cast<float>(value(reverb::width));
        wet_same_ = wet_scale * mix * (width / 2.0F + 0.5F);
        wet_cross_ = wet_scale * mix * ((1.0F - width) / 2.0F);
        dry_ = 1.0F - mix;
    }

    void render(float* left, float* right, std::size_t frames) noexcept override {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const float input = (left[frame] + right[frame]) * input_gain;
            float out[2] = {0.0F, 0.0F};
            for (std::size_t channel = 0; channel < 2; ++channel) {
                float sum = 0.0F;
                for (auto& comb : combs_[channel]) sum += comb.run(input, feedback_, damp_);
                for (auto& allpass : allpasses_[channel]) sum = allpass.run(sum);
                out[channel] = sum;
            }
            left[frame] = dry_ * left[frame] + wet_same_ * out[0] + wet_cross_ * out[1];
            right[frame] = dry_ * right[frame] + wet_same_ * out[1] + wet_cross_ * out[0];
        }
    }

private:
    static double room_feedback(double size) noexcept { return 0.7 + 0.28 * size; }

    std::array<std::array<Comb, 8>, 2> combs_;
    std::array<std::array<Allpass, 4>, 2> allpasses_;
    float feedback_ = 0.0F;
    float damp_ = 0.0F;
    float wet_same_ = 0.0F;
    float wet_cross_ = 0.0F;
    float dry_ = 1.0F;
};

} // namespace

std::unique_ptr<PluginInstance> make_reverb() { return std::make_unique<Reverb>(); }

} // namespace blokkily::effects
