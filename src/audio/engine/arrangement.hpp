#pragma once

// One compiled arrangement (private to SongEngine). Four of these are owned
// for the life of the engine, so a recompile always has a slot to fill that is
// neither queued, nor being rendered, nor being retired by the callback as it
// moves onto a new one, and publishing one costs a compare-and-swap on the
// handoff word instead of an allocation the render callback would wait for.

#include "engine_automation.hpp"
#include "engine_clips.hpp"
#include "engine_metronome.hpp"

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
    // What the transport handed to each processor reads (item 2.4): beats
    // are counted in these ticks, and bars by this meter.
    Tick ticks_per_beat = 480;
    MeterMap meter;
    ArrangementClips clips;
    ArrangementAutomation automation;
    // Every beat of the click (item 3.7), placed by `clock`.
    ArrangementClicks clicks;
};

} // namespace blokkily::engine
