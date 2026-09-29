// features/dynamic_modulation.feature: Modulation keeps the real-time rules.
//
// Wave 5.1 in the production SongEngine::process(): a CLAP instrument moved by
// an LFO, a macro and an envelope follower of a second track, a thousand
// process() calls across loop wraps, a seek, a stop and a start, with the
// macro turned and a depth changed from the control thread (apply_mix) and
// one recompile that removes a modulator and adds another, must not allocate
// or free. The audio is then checked, so the zero comes from an engine that
// modulated something.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <vector>

namespace {

using namespace blokkily;
using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;
constexpr double rate = 48000.0;

BLOKKILY_REALTIME_CASE(dynamic_modulation) {
    Pattern pattern(1920, 480);
    for (Tick start = 0; start < 1920; start += 24) {
        Trigger trigger;
        trigger.start = start;
        trigger.duration = 24;
        trigger.musical_data = Note{60, 1.0F, 0.0F};
        (void)pattern.add(trigger);
    }
    Song song;
    song.patterns = {{"P", pattern}};
    song.tracks = {Track{}, Track{}};
    song.clips = {{0, 0, 0, 1}, {1, 0, 0, 1}};

    Modulator lfo;
    lfo.kind = Modulator::Kind::lfo;
    lfo.shape = LfoShape::sample_and_hold;
    lfo.rate_hz = 7.0;
    lfo.targets = {{track_instrument(0), 0, 0.2}};
    Modulator macro;
    macro.kind = Modulator::Kind::macro;
    macro.value = 0.5;
    macro.targets = {{track_instrument(0), 0, 0.1}, {track_instrument(1), 0, -0.1}};
    Modulator follower;
    follower.kind = Modulator::Kind::follower;
    follower.source_track = 1;
    follower.targets = {{track_instrument(0), 0, -0.2}};
    song.modulators = {lfo, macro, follower};
    require(song.consistent(), "the song is valid");

    SongEngine engine;
    for (std::size_t track = 0; track < 2; ++track) {
        std::string error;
        auto instance = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
        require(instance != nullptr, "the CLAP fixture loads: " + error);
        engine.set_instrument(track, std::move(instance));
    }
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "engine prepare: " + error);
    // The recompiled song is built before the loop: building it allocates.
    auto changed = song;
    changed.modulators.erase(changed.modulators.begin());
    changed.modulators.push_back(lfo);
    changed.modulators.back().shape = LfoShape::triangle;
    auto turned = song;
    turned.modulators[1].value = 0.9;
    turned.modulators[1].targets[0].depth = 0.3;

    engine.set_playing(true);
    std::vector<float> left(calls * block, 0.0F);
    std::vector<float> right(calls * block, 0.0F);
    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        // The control thread's moves happen between blocks, outside the
        // guard: only the callback is held to the rules.
        if (call == 200) engine.apply_mix(turned);
        if (call == 400) require(engine.recompile(changed, 0, &error), "recompile: " + error);
        if (call == 600) engine.seek(12345);
        if (call == 700) engine.set_playing(false);
        if (call == 750) engine.set_playing(true);
        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        {
            test::AllocationGuard guard;
            engine.process({out_left, out_right});
            total += guard.count();
        }
    }
    require_no_allocations(total, "1000 SongEngine::process calls with modulators");
    // It played, and the level moved: the LFO steps it every cycle.
    float lowest = 1.0F;
    float highest = 0.0F;
    for (std::size_t frame = 1000; frame < 200 * block; ++frame) {
        lowest = std::min(lowest, left[frame]);
        highest = std::max(highest, left[frame]);
    }
    require(highest > 0.1F && highest - lowest > 0.05F, "the modulated instruments are heard");
}

} // namespace
