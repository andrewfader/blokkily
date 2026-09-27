#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/modulator.hpp"
#include "blokkily/audio/song_engine.hpp"

#include <vector>

namespace {

using namespace blokkily;
using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;
constexpr double rate = 48000.0;

BLOKKILY_REALTIME_CASE(dynamic_modulation) {
    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks = {Track{}};

    SongEngine engine;
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "engine prepare: " + error);

    ModulationMatrix matrix;
    LfoConfig lfo_cfg;
    lfo_cfg.waveform = LfoWaveform::sine;
    lfo_cfg.frequency_hz = 5.0;
    lfo_cfg.depth = 0.8F;
    std::size_t lfo_id = matrix.add_lfo(lfo_cfg);

    EnvelopeFollowerConfig env_cfg;
    env_cfg.attack_ms = 10.0F;
    env_cfg.release_ms = 80.0F;
    std::size_t env_id = matrix.add_envelope_follower(env_cfg);

    std::size_t macro_id = matrix.add_macro("Macro 1", 0.5F);

    const ProcessorAddress inst_addr = track_instrument(0);
    const ProcessorAddress insert_addr{BusKind::track, 0, 0};

    matrix.route_lfo(lfo_id, {inst_addr, 1, 1.0F});
    matrix.route_envelope_follower(env_id, {inst_addr, 2, 0.5F});
    matrix.macro(macro_id).add_target({insert_addr, 3, 0.0F, 1.0F});

    engine.set_modulation_matrix(&matrix);
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

    require_no_allocations(total, "1000 SongEngine::process calls with dynamic modulation matrix");
}

} // namespace
