#pragma once

// Test-only access to SongEngine internals. Only tests include this header;
// production code never does.

#include "track_playback.hpp"

#include "blokkily/audio/song_engine.hpp"

#include <cstddef>

namespace blokkily::engine {

struct TestAccess {
    // Installs a source on a track's clip stage (chunk stage 5). Control
    // thread, before playback or while the device is stopped.
    static void set_test_source(SongEngine& engine, std::size_t track,
                                TestSourceFunction source, void* context) {
        auto& playback = *engine.tracks_.at(track);
        playback.clips.test_source = source;
        playback.clips.test_context = context;
    }
    // The capacity of a track's preallocated event scratch.
    [[nodiscard]] static std::size_t event_capacity(const SongEngine& engine,
                                                    std::size_t track) {
        return engine.tracks_.at(track)->events.size();
    }
};

} // namespace blokkily::engine
