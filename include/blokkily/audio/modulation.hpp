#pragma once

// The signal side of the song's modulators (model/modulation.hpp): what an
// LFO's shape is at a point of its cycle, and how a follower tracks a level.
// Both are pure and real-time safe; the engine calls them on the audio
// thread (src/audio/engine/engine_modulation.hpp) and nothing else keeps any
// state for them.

#include "blokkily/model/modulation.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <cstddef>

namespace blokkily {

// The LFO's output, -1..+1, at `phase` cycles from its start. Every shape
// starts its cycle at phase 0: sine and triangle rise from 0, the saws start
// at their extremes, the square is high for the first half. The random shape
// (sample and hold) holds one value per cycle, the same value for the same
// cycle every time, so a bounce is the mix that was auditioned.
[[nodiscard]] float lfo_value(LfoShape shape, double phase) noexcept;

// An envelope follower: the block's peak, rising with the attack time and
// falling with the release time (one-pole, per block). The level is 0..1.
class EnvelopeFollower {
public:
    // Follows one block of audio and returns the level after it.
    float follow(StereoBlock audio, double sample_rate, double attack_ms,
                 double release_ms) noexcept;
    [[nodiscard]] float level() const noexcept { return level_; }
    void reset() noexcept { level_ = 0.0F; }

private:
    float level_ = 0.0F;
};

} // namespace blokkily
