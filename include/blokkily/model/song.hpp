#pragma once

#include "blokkily/model/pattern.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace blokkily {

// One instrument the project expects to reconstruct on load. The state blob is
// whatever the adapter's save_state() produced, kept opaque here so that
// format-specific details stay behind the plugin adapters.
struct InstrumentSlot {
    std::string format;     // "CLAP", "VST3", or "SoundFont"
    std::string path;       // module, bundle, or SF2/SF3 file
    std::string identifier; // CLAP plugin id or VST3 identifier; may be empty
    std::vector<std::byte> state;
};

// A channel strip. Gain is in decibels because that is the unit a mixer is
// operated in; the engine converts once, at the boundary.
struct MixerStrip {
    double gain_db = 0.0;
    double pan = 0.0; // -1 hard left, 0 centre, +1 hard right
    bool mute = false;
    bool solo = false;
};

struct Track {
    std::string name = "Track";
    InstrumentSlot instrument;
    MixerStrip mix;
};

struct PatternSlot {
    std::string name = "Pattern";
    Pattern pattern{1920, 480};
};

// One placement of a pattern on one track's timeline. `repeats` is how many
// times the pattern runs back to back from `start`; each repetition counts as
// the next loop, so probability and loop conditions keep working in a song.
struct Clip {
    std::size_t track = 0;
    std::size_t pattern = 0;
    Tick start = 0;
    std::uint32_t repeats = 1;
};

// The arrangement: named patterns, tracks that play them, and the clips that
// say when. A song with one track, one pattern, and one clip is the pattern
// sequencer this grew out of, so nothing has to opt in to the timeline.
struct Song {
    std::vector<PatternSlot> patterns{PatternSlot{}};
    std::vector<Track> tracks{Track{}};
    std::vector<Clip> clips{Clip{}};
    double master_gain_db = 0.0;

    [[nodiscard]] Pattern& pattern(std::size_t index = 0) { return patterns.at(index).pattern; }
    [[nodiscard]] const Pattern& pattern(std::size_t index = 0) const {
        return patterns.at(index).pattern;
    }
    // Ticks from the start of the song to the end of its last clip. A song
    // whose clips are all empty still lasts one pattern, so transport has
    // somewhere to run.
    [[nodiscard]] Tick length() const;
    [[nodiscard]] bool any_solo() const;
    // True when every clip names a track and a pattern that exist.
    [[nodiscard]] bool consistent() const;
    // Compiles one track's whole timeline into song-absolute ticks, expanding
    // every clip repetition. Returns nothing the track cannot play.
    [[nodiscard]] ScheduledEvents arrange(std::size_t track, std::uint64_t seed = 0) const;
};

} // namespace blokkily
