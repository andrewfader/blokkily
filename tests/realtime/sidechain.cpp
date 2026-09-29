// features/sidechain.feature: A sidechain keeps the real-time rules.
//
// Wave 5.2 in the production SongEngine::process(): a built-in compressor on
// track 0 keyed by track 1, which renders first although it comes later and
// is muted. A thousand process() calls across loop wraps, a seek, and a
// recompile that moves the key to the master bus's compressor, must not
// allocate or free. The audio is checked afterwards: the key ducked.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "audio/engine/test_access.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/effects/builtin.hpp"

#include <vector>

namespace {

using namespace blokkily;
using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;
constexpr double rate = 48000.0;

EffectSlot compressor() {
    EffectSlot slot;
    slot.plugin = {std::string(builtin_effect_format), "", "compressor", {}};
    return slot;
}

void pulse(void*, StereoBlock track, std::uint64_t position) noexcept {
    for (std::size_t frame = 0; frame < track.left.size(); ++frame)
        track.left[frame] = track.right[frame] = (position + frame) % 24000 < 6000 ? 1.0F : 0.0F;
}

void steady(void* context, StereoBlock track, std::uint64_t) noexcept {
    const float level = *static_cast<float*>(context);
    for (std::size_t frame = 0; frame < track.left.size(); ++frame)
        track.left[frame] = track.right[frame] = level;
}

BLOKKILY_REALTIME_CASE(sidechain) {
    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks = {Track{}, Track{}};
    song.clips = {{0, 0, 0, 1}, {1, 0, 0, 1}};
    song.tracks[0].inserts = {compressor()};
    song.tracks[0].inserts[0].sidechain = 1;
    song.tracks[1].mix.mute = true;
    // The master's compressor sits bypassed until the recompile keys it.
    song.master_inserts = {compressor()};
    song.master_inserts[0].bypass = true;

    SongEngine engine;
    engine.set_processor({BusKind::track, 0, 0}, create_builtin_effect("compressor"));
    engine.set_processor({BusKind::master, 0, 0}, create_builtin_effect("compressor"));
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "engine prepare: " + error);
    float level = 0.5F;
    engine::TestAccess::set_test_source(engine, 0, steady, &level);
    engine::TestAccess::set_test_source(engine, 1, pulse, nullptr);
    auto moved = song;
    moved.tracks[0].inserts[0].sidechain.reset();
    moved.master_inserts[0].sidechain = 1;
    moved.master_inserts[0].bypass = false;

    engine.set_playing(true);
    std::vector<float> left(calls * block, 0.0F);
    std::vector<float> right(calls * block, 0.0F);
    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        if (call == 500) engine.seek(777);
        if (call == 800) require(engine.recompile(moved, 0, &error), "recompile: " + error);
        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        {
            test::AllocationGuard guard;
            engine.process({out_left, out_right});
            total += guard.count();
        }
    }
    require_no_allocations(total, "1000 SongEngine::process calls with a sidechain key");
    const auto rendered = std::span<const float>(left);
    const float keyed = probe::peak(rendered.subspan(3000, 2000));
    const float open = probe::peak(rendered.subspan(20000, 2000));
    require(keyed < 0.2F && open > 0.3F, "the key ducked the track and let it go");
}

} // namespace
