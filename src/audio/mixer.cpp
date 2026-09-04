#include "blokkily/audio/mixer.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace blokkily {

double db_to_linear(double decibels) noexcept {
    if (decibels <= minimum_audible_db) return 0.0;
    return std::pow(10.0, decibels / 20.0);
}

double linear_to_db(double linear) noexcept {
    if (linear <= 0.0) return minimum_audible_db;
    return std::max(minimum_audible_db, 20.0 * std::log10(linear));
}

bool audible(const MixerStrip& strip, bool any_solo) noexcept {
    if (strip.mute) return false;
    return !any_solo || strip.solo;
}

StripGain strip_gain(const MixerStrip& strip, bool any_solo) noexcept {
    if (!audible(strip, any_solo)) return {0.0F, 0.0F};
    const double linear = db_to_linear(strip.gain_db);
    const double position = std::clamp(strip.pan, -1.0, 1.0);
    const double angle = (position + 1.0) * 0.25 * std::numbers::pi;
    return {static_cast<float>(linear * std::cos(angle)),
            static_cast<float>(linear * std::sin(angle))};
}

float mix_into(StereoBlock bus, std::span<const float> track_left,
               std::span<const float> track_right, StripGain gain) noexcept {
    const auto frames = std::min({bus.left.size(), bus.right.size(),
                                  track_left.size(), track_right.size()});
    float peak = 0.0F;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float left = track_left[frame] * gain.left;
        const float right = track_right[frame] * gain.right;
        bus.left[frame] += left;
        bus.right[frame] += right;
        peak = std::max({peak, std::abs(left), std::abs(right)});
    }
    return peak;
}

float apply_master(StereoBlock bus, float master_gain) noexcept {
    const auto frames = std::min(bus.left.size(), bus.right.size());
    float peak = 0.0F;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        bus.left[frame] *= master_gain;
        bus.right[frame] *= master_gain;
        peak = std::max({peak, std::abs(bus.left[frame]), std::abs(bus.right[frame])});
    }
    return peak;
}

} // namespace blokkily
