#pragma once

#include "blokkily/model/song.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <span>

namespace blokkily {

// Per-channel linear gain for one strip. Computed once on the control thread
// and read by the audio thread, so the mixer never converts decibels or takes
// a trigonometric function inside the render callback.
struct StripGain {
    float left = 1.0F;
    float right = 1.0F;

    [[nodiscard]] bool silent() const noexcept { return left == 0.0F && right == 0.0F; }
};

// Anything at or below this reads as off rather than as a very quiet signal,
// so a fader pulled to the bottom is actually silent.
inline constexpr double minimum_audible_db = -96.0;

[[nodiscard]] double db_to_linear(double decibels) noexcept;
[[nodiscard]] double linear_to_db(double linear) noexcept;

// A strip is audible unless it is muted, or some other strip is soloed and it
// is not. Solo is evaluated against the whole song, which is why the caller
// passes the answer in.
[[nodiscard]] bool audible(const MixerStrip& strip, bool any_solo) noexcept;

// Constant-power panning: a centred strip sits 3 dB down on each side so that
// sweeping across the image keeps the same perceived loudness. An inaudible
// strip returns zero gain rather than a flag the caller could forget to check.
[[nodiscard]] StripGain strip_gain(const MixerStrip& strip, bool any_solo) noexcept;

// Adds one already-rendered track into the mix bus and reports the peak the
// track contributed. Allocation-free and lock-free: this is render-callback
// code.
[[nodiscard]] float mix_into(StereoBlock bus, std::span<const float> track_left,
                             std::span<const float> track_right, StripGain gain) noexcept;

// Scales the finished bus by the master gain and reports its peak.
[[nodiscard]] float apply_master(StereoBlock bus, float master_gain) noexcept;

} // namespace blokkily
