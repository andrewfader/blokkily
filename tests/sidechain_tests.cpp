#include "audio/engine/test_access.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/sidechain.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/effects/builtin.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

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

void compression_case() {
    constexpr double rate = 48000.0;
    constexpr std::uint32_t block = 256;

    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    // Track 0 = Kick (sidechain source), Track 1 = Bass (compressed)
    song.tracks = {Track{}, Track{}};
    song.tracks[1].inserts = {slot("Built-in", "compressor")};

    SongEngine engine;
    auto comp = create_builtin_effect("compressor");
    require(comp != nullptr, "compressor must be instantiated");
    const ProcessorAddress comp_addr{BusKind::track, 1, 0};
    engine.set_processor(comp_addr, std::move(comp));

    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);

    // Route Track 0 into Track 1's compressor insert
    engine.set_sidechain_route(comp_addr, 0);

    // Test sources for tracks
    float kick_level = 0.0F;
    float bass_level = 0.5F;
    engine::TestAccess::set_test_source(engine, 0, test_source_signal, &kick_level);
    engine::TestAccess::set_test_source(engine, 1, test_source_signal, &bass_level);

    engine.set_playing(true);

    std::vector<float> left(block), right(block);
    StereoBlock out{left, right};

    // 1. Kick silent: bass is uncompressed
    for (int b = 0; b < 10; ++b) {
        engine.process(out);
    }
    const float uncompressed_peak = engine.track_peak(1);
    require(uncompressed_peak > 0.3F, "bass sounds when kick is silent");

    // 2. Kick loud (1.0 = 0 dBFS, threshold is -20 dB): bass gets ducked
    kick_level = 1.0F;
    for (int b = 0; b < 20; ++b) {
        engine.process(out);
    }
    const float ducked_peak = engine.track_peak(1);
    require(ducked_peak < uncompressed_peak * 0.75F,
            "bass must be attenuated when kick triggers sidechain: ducked=" +
                std::to_string(ducked_peak) + " uncompressed=" + std::to_string(uncompressed_peak));
}

void topological_sort_case() {
    // 4 tracks: 0, 1, 2, 3
    // Dependencies: Track 0 depends on Track 2; Track 2 depends on Track 3.
    // Order must have: 3 before 2, and 2 before 0.
    std::vector<SidechainRoute> scs{
        {2, ProcessorAddress{BusKind::track, 0, 0}},
        {3, ProcessorAddress{BusKind::track, 2, 0}}
    };
    std::vector<MultiOutputRoute> mos;

    auto order = compute_track_render_order(4, scs, mos);
    require(order.size() == 4, "all 4 tracks must be in render order");

    auto pos = [&](std::size_t trk) {
        auto it = std::find(order.begin(), order.end(), trk);
        return static_cast<std::size_t>(std::distance(order.begin(), it));
    };

    require(pos(3) < pos(2), "track 3 must render before track 2");
    require(pos(2) < pos(0), "track 2 must render before track 0");

    // Test cycle handling: 0 -> 1 and 1 -> 0
    std::vector<SidechainRoute> cycle{
        {0, ProcessorAddress{BusKind::track, 1, 0}},
        {1, ProcessorAddress{BusKind::track, 0, 0}}
    };
    auto cycle_order = compute_track_render_order(2, cycle, mos);
    require(cycle_order.size() == 2, "cycle must safely resolve with all tracks included");
}

void multi_output_case() {
    constexpr double rate = 48000.0;
    constexpr std::uint32_t block = 256;

    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    // Track 0 = Source, Track 1 = Dest
    song.tracks = {Track{}, Track{}};

    SongEngine engine;
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);

    float src_level = 0.4F;
    engine::TestAccess::set_test_source(engine, 0, test_source_signal, &src_level);

    // Route Track 0 to Track 1
    engine.set_multi_output_route(1, 0, 1, 1.0F);
    engine.set_playing(true);

    std::vector<float> left(block), right(block);
    StereoBlock out{left, right};

    engine.process(out);

    const float dest_peak = engine.track_peak(1);
    require(dest_peak >= 0.19F, "dest track receives multi-output signal: " + std::to_string(dest_peak));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: blokkily_sidechain_tests <case>\n";
        return 2;
    }
    const std::string name = argv[1];
    try {
        if (name == "compression") compression_case();
        else if (name == "topological_sort") topological_sort_case();
        else if (name == "multi_output") multi_output_case();
        else {
            std::cerr << "Unknown case: " << name << "\n";
            return 2;
        }
    } catch (const std::exception& ex) {
        std::cerr << "FAILED: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
