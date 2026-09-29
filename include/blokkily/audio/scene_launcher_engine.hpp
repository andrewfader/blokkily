#pragma once

// What the scene launcher (phase 2, wave 6.1) says about itself from the
// render callback. The launcher plays inside SongEngine: its grid is compiled
// from Song::launcher with the arrangement and handed to the callback through
// the same handoff, launches and stops travel to the callback through a
// lock-free queue (SongEngine::launch_cell and friends), and these two types
// come back.

#include "blokkily/model/timebase.hpp"

#include <cstddef>
#include <cstdint>

namespace blokkily {

// One stretch of a track the launcher played from one cell while arrangement
// recording was on: `pattern` from `start` to `end`, in song ticks counted
// from where the transport started rolling (they run on past the song's end
// when the transport wraps). The loops of a probabilistic or conditional
// pattern repeat every `cycle` loops while launched, so printing the take as
// clips of at most `cycle` repeats plays exactly what was heard.
struct LauncherTake {
    std::uint32_t track = 0;
    std::uint32_t scene = 0;
    std::size_t pattern = 0;
    Tick start = 0;
    Tick end = 0;
    std::uint32_t cycle = 1;

    friend bool operator==(const LauncherTake&, const LauncherTake&) = default;
};

// Where one track's launcher is, as of the last block the callback rendered.
struct LauncherTrackStatus {
    bool playing = false;
    bool queued_play = false;
    bool queued_stop = false;
    std::uint32_t scene = 0;        // the scene playing (when playing)
    std::uint32_t queued_scene = 0; // the scene waiting (when queued_play)

    friend bool operator==(const LauncherTrackStatus&, const LauncherTrackStatus&) = default;
};

} // namespace blokkily
