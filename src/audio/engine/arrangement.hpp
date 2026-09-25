#pragma once

// One compiled arrangement (private to SongEngine). Three of these are owned
// for the life of the engine, so a recompile always has a slot to fill that is
// neither being rendered nor already queued, and publishing one costs a pointer
// store instead of an allocation the render callback would have to wait for.

#include "engine_automation.hpp"
#include "engine_clips.hpp"

#include "blokkily/audio/event_timeline.hpp"
#include "blokkily/model/timebase.hpp"

#include <cstdint>
#include <vector>

namespace blokkily::engine {

struct Arrangement {
    // A sample timeline per track.
    std::vector<std::vector<TimedPluginEvent>> timelines;
    std::uint64_t song_samples = 0;
    // Where every tick of this arrangement falls. The playhead keeps its tick
    // when an arrangement with another clock replaces this one.
    TickClock clock;
    ArrangementClips clips;
    ArrangementAutomation automation;
};

} // namespace blokkily::engine
