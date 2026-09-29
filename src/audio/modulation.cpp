#include "blokkily/audio/modulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace blokkily {

float lfo_value(LfoShape shape, double phase) noexcept {
    if (!std::isfinite(phase)) return 0.0F;
    const double cycle = std::floor(phase);
    const double p = phase - cycle; // 0 <= p < 1
    switch (shape) {
    case LfoShape::sine: return static_cast<float>(std::sin(2.0 * std::numbers::pi * p));
    case LfoShape::triangle:
        if (p < 0.25) return static_cast<float>(4.0 * p);
        if (p < 0.75) return static_cast<float>(2.0 - 4.0 * p);
        return static_cast<float>(4.0 * p - 4.0);
    case LfoShape::saw_up: return static_cast<float>(2.0 * p - 1.0);
    case LfoShape::saw_down: return static_cast<float>(1.0 - 2.0 * p);
    case LfoShape::square: return p < 0.5 ? 1.0F : -1.0F;
    case LfoShape::sample_and_hold: {
        // A hash of the cycle number (splitmix64): the same cycle always
        // holds the same value.
        auto x = static_cast<std::uint64_t>(static_cast<std::int64_t>(cycle)) +
                 0x9E3779B97F4A7C15ULL;
        x = (x ^ (x >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        x = (x ^ (x >> 27U)) * 0x94D049BB133111EBULL;
        x ^= x >> 31U;
        return static_cast<float>(static_cast<double>(x >> 11U) / 9007199254740992.0 * 2.0 -
                                  1.0);
    }
    }
    return 0.0F;
}

float EnvelopeFollower::follow(StereoBlock audio, double sample_rate, double attack_ms,
                               double release_ms) noexcept {
    const auto frames = std::min(audio.left.size(), audio.right.size());
    if (frames == 0 || !(sample_rate > 0.0)) return level_;
    float peak = 0.0F;
    for (std::size_t frame = 0; frame < frames; ++frame)
        peak = std::max({peak, std::abs(audio.left[frame]), std::abs(audio.right[frame])});
    peak = std::min(peak, 1.0F);
    const double time_ms = std::max(0.1, peak > level_ ? attack_ms : release_ms);
    const double keep = std::exp(-static_cast<double>(frames) / (sample_rate * time_ms / 1000.0));
    level_ = static_cast<float>(keep * level_ + (1.0 - keep) * peak);
    return level_;
}

} // namespace blokkily
