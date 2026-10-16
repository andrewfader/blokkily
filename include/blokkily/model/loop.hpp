#pragma once

// The arrangement loop: while it is on, playback wraps at `end` back to
// `start` instead of at the end of the song. An export still renders the
// whole arrangement. The settings are saved with the song and reach the
// running engine without rebuilding the graph, like the metronome.

#include "blokkily/model/event.hpp"

namespace blokkily {

struct ArrangementLoop {
    bool enabled = false;
    Tick start = 0;
    Tick end = 0; // exclusive; must be greater than start while enabled
    // Punch (features/comping_and_punch.feature): while on, recording keeps
    // only what is played between `start` and `end` - the same IN and OUT -
    // whether the loop is on or not. Looped, every pass is a take.
    bool punch = false;

    [[nodiscard]] bool active() const noexcept { return enabled && end > start; }
    [[nodiscard]] bool punching() const noexcept { return punch && end > start; }

    friend bool operator==(const ArrangementLoop&, const ArrangementLoop&) = default;
};

} // namespace blokkily
