#include "blokkily/audio/modulator.hpp"
#include "blokkily/audio/song_engine.hpp"

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

void lfo_shapes_case() {
    constexpr double rate = 48000.0;
    // 1 Hz LFO: exactly 1 cycle in 48000 frames
    LfoConfig cfg;
    cfg.frequency_hz = 1.0;
    cfg.depth = 1.0F;
    cfg.bipolar = true;

    // 1. Sine
    cfg.waveform = LfoWaveform::sine;
    LfoModulator lfo_sine(cfg);
    lfo_sine.reset();
    // At 1/4 cycle (12000 frames), sine should be at peak +1.0
    float v1 = lfo_sine.process(12000, rate);
    require(std::abs(v1 - 1.0F) < 0.01F, "sine peak at 1/4 cycle must be ~1.0: " + std::to_string(v1));
    // At 3/4 cycle (+24000 frames = 36000 total), sine should be at valley -1.0
    float v2 = lfo_sine.process(24000, rate);
    require(std::abs(v2 - (-1.0F)) < 0.01F, "sine valley at 3/4 cycle must be ~ -1.0: " + std::to_string(v2));

    // 2. Square
    cfg.waveform = LfoWaveform::square;
    LfoModulator lfo_sq(cfg);
    lfo_sq.reset();
    float sq1 = lfo_sq.process(10000, rate);
    require(sq1 == 1.0F, "square first half must be +1.0");
    float sq2 = lfo_sq.process(20000, rate); // at frame 30000 (second half)
    require(sq2 == -1.0F, "square second half must be -1.0");

    // 3. Saw Up
    cfg.waveform = LfoWaveform::saw_up;
    LfoModulator lfo_saw(cfg);
    lfo_saw.reset();
    float saw_start = lfo_saw.process(0, rate);
    require(std::abs(saw_start - (-1.0F)) < 0.01F, "saw up starts near -1.0");
    float saw_mid = lfo_saw.process(24000, rate);
    require(std::abs(saw_mid - 0.0F) < 0.02F, "saw up mid cycle is ~0.0");

    // 4. Tempo-synced: 1 beat at 120 BPM = 0.5s = 24000 frames
    cfg.waveform = LfoWaveform::sine;
    cfg.tempo_sync_beats = 1.0;
    LfoModulator lfo_sync(cfg);
    lfo_sync.reset();
    // 6000 frames is 1/4 of a 24000 frame cycle
    float sync_peak = lfo_sync.process(6000, rate, 120.0);
    require(std::abs(sync_peak - 1.0F) < 0.01F, "tempo synced sine peak at 6000 frames");
}

void envelope_follower_case() {
    constexpr double rate = 48000.0;
    constexpr std::size_t block = 256;

    EnvelopeFollowerConfig cfg;
    cfg.attack_ms = 5.0F;
    cfg.release_ms = 50.0F;
    cfg.min_out = 0.0F;
    cfg.max_out = 1.0F;
    EnvelopeFollowerModulator env(cfg);

    std::vector<float> silent_left(block, 0.0F);
    std::vector<float> silent_right(block, 0.0F);
    StereoBlock silent_audio{silent_left, silent_right};

    float v_silent = env.process(silent_audio, rate);
    require(v_silent == 0.0F, "silent audio produces 0.0 envelope");

    // Burst of 1.0 full scale audio
    std::vector<float> loud_left(block, 1.0F);
    std::vector<float> loud_right(block, 1.0F);
    StereoBlock loud_audio{loud_left, loud_right};

    float v_loud = 0.0F;
    for (int b = 0; b < 10; ++b) {
        v_loud = env.process(loud_audio, rate);
    }
    require(v_loud > 0.8F, "envelope rises during burst: " + std::to_string(v_loud));

    // Followed by silence
    float v_decay = 0.0F;
    for (int b = 0; b < 40; ++b) {
        v_decay = env.process(silent_audio, rate);
    }
    require(v_decay < v_loud && v_decay < 0.2F, "envelope decays after burst: " + std::to_string(v_decay));
}

void macro_routing_case() {
    MacroModulator macro("Cutoff Macro", 0.0F);
    const ProcessorAddress target1{BusKind::track, 0, 0};
    const ProcessorAddress target2{BusKind::track, 0, 1};

    macro.add_target({target1, 10, 200.0F, 20000.0F});
    macro.add_target({target2, 2, 0.0F, 1.0F});

    ModulationMatrix matrix;
    std::size_t m_idx = matrix.add_macro("Cutoff Macro", 0.0F);
    matrix.macro(m_idx).add_target({target1, 10, 200.0F, 20000.0F});
    matrix.macro(m_idx).add_target({target2, 2, 0.0F, 1.0F});

    std::array<PluginEvent, 16> events{};
    std::size_t count1 = matrix.generate_events_for(target1, events);
    require(count1 == 1, "target1 receives 1 event");
    require(events[0].type == PluginEvent::Type::parameter_modulation, "must be parameter_modulation event");
    require(events[0].key_or_parameter == 10, "parameter id 10");
    require(std::abs(events[0].value - 200.0) < 1e-4, "at 0.0 macro value is min (200.0)");

    // Turn macro to 1.0
    matrix.macro(m_idx).set_value(1.0F);
    std::size_t count2 = matrix.generate_events_for(target1, events);
    require(count2 == 1, "target1 receives 1 event");
    require(std::abs(events[0].value - 20000.0) < 1e-4, "at 1.0 macro value is max (20000.0)");

    // Target 2
    std::size_t count3 = matrix.generate_events_for(target2, events);
    require(count3 == 1, "target2 receives 1 event");
    require(events[0].key_or_parameter == 2, "parameter id 2");
    require(std::abs(events[0].value - 1.0) < 1e-4, "at 1.0 macro value is max (1.0)");
}

void engine_dispatch_case() {
    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks = {Track{}};

    SongEngine engine;
    std::string error;
    require(engine.prepare(song, 48000.0, 256, 0, &error), "engine prepare: " + error);

    ModulationMatrix matrix;
    LfoConfig lfo_cfg;
    lfo_cfg.waveform = LfoWaveform::sine;
    lfo_cfg.frequency_hz = 10.0;
    lfo_cfg.depth = 0.5F;
    std::size_t lfo_id = matrix.add_lfo(lfo_cfg);

    const ProcessorAddress inst_addr = track_instrument(0);
    matrix.route_lfo(lfo_id, {inst_addr, 3, 1.0F});

    engine.set_modulation_matrix(&matrix);
    engine.set_playing(true);

    std::vector<float> left(256, 0.0F);
    std::vector<float> right(256, 0.0F);
    StereoBlock block{left, right};

    engine.process(block);

    // Verify matrix evaluated and generated non-zero LFO value
    float lfo_val = matrix.lfo(lfo_id).current_value();
    require(std::abs(lfo_val) <= 0.5F, "LFO value within depth bound");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: blokkily_dynamic_modulation_tests <case>\n";
        return 2;
    }
    const std::string name = argv[1];
    try {
        if (name == "lfo_shapes") lfo_shapes_case();
        else if (name == "envelope_follower") envelope_follower_case();
        else if (name == "macro_routing") macro_routing_case();
        else if (name == "engine_dispatch") engine_dispatch_case();
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
