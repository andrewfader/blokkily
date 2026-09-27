#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "audio/engine/test_access.hpp"
#include "blokkily/audio/sidechain.hpp"
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

EffectSlot slot(const char* format, const char* identifier) {
    return {{format, "", identifier, {}}, false, {}};
}

void test_source_signal(void* context, StereoBlock track, std::uint64_t) noexcept {
    const float level = *static_cast<float*>(context);
    for (std::size_t i = 0; i < track.left.size(); ++i) {
        track.left[i] = level;
        track.right[i] = level;
    }
}

BLOKKILY_REALTIME_CASE(sidechain) {
    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks = {Track{}, Track{}};
    song.tracks[1].inserts = {slot("Built-in", "compressor")};

    SongEngine engine;
    auto comp = create_builtin_effect("compressor");
    require(comp != nullptr, "compressor must be instantiated");
    const ProcessorAddress comp_addr{BusKind::track, 1, 0};
    engine.set_processor(comp_addr, std::move(comp));

    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "engine prepare: " + error);

    // Route Track 0 into Track 1's compressor insert
    engine.set_sidechain_route(comp_addr, 0);

    // Multi-output route from Track 0 to Track 1
    engine.set_multi_output_route(1, 0, 1, 0.5F);

    float sig_level = 0.5F;
    engine::TestAccess::set_test_source(engine, 0, test_source_signal, &sig_level);
    engine::TestAccess::set_test_source(engine, 1, test_source_signal, &sig_level);

    engine.set_playing(true);

    std::vector<float> left(calls * block, 0.0F);
    std::vector<float> right(calls * block, 0.0F);
    test::AllocationCount total;

    for (std::size_t call = 0; call < calls; ++call) {
        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        {
            test::AllocationGuard guard;
            engine.process({out_left, out_right});
            total += guard.count();
        }
    }

    require_no_allocations(total, "1000 SongEngine::process calls with sidechain and multi-output routing");
}

} // namespace
