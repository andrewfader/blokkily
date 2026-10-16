// features/arpeggiator.feature: the arpeggiator keeps the real-time rules.
//
// A track holding a chord through an arpeggiator, in the production
// SongEngine::process(): a thousand calls across a seek, a stop and a start,
// and settings changed while it plays, must not allocate or free.

#include "realtime_case.hpp"

#include "blokkily/audio/song_engine.hpp"

#include <vector>

namespace {

using namespace blokkily;
using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;

BLOKKILY_REALTIME_CASE(arpeggiator) {
    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    for (const std::int16_t key : {60, 64, 67, 71}) {
        Trigger trigger;
        trigger.duration = 960;
        trigger.musical_data = Note{key, 0.8F};
        (void)song.patterns[0].pattern.add(trigger);
    }
    song.clips = {{0, 0, 0, 4}};
    song.tracks[0].arp.enabled = true;
    song.tracks[0].arp.octaves = 4;
    song.tracks[0].arp.division = 8;
    SongEngine engine;
    std::string error;
    require(engine.prepare(song, 48000.0, block, 0, &error), "engine prepare: " + error);
    auto changed = song;
    changed.tracks[0].arp.mode = Arpeggiator::Mode::random;
    changed.tracks[0].arp.latch = true;
    engine.set_playing(true);
    std::vector<float> left(block), right(block);
    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        // The settings travel by atomic store, as a fader's do.
        if (call == 300) engine.apply_mix(changed);
        if (call == 500) engine.seek(777);
        if (call == 700) engine.set_playing(false);
        if (call == 800) engine.set_playing(true);
        test::AllocationGuard guard;
        engine.process({left, right});
        total += guard.count();
    }
    require_no_allocations(total, "1000 SongEngine::process calls through an arpeggiator");
}

} // namespace
