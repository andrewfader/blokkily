// features/effects.feature: Effects keep the real-time rules.
//
// The insert chains, sends, return buses, master inserts and delay
// compensation (item 2.4) in the production SongEngine::process(): the CLAP
// and VST3 effect fixtures through their production adapters and every
// built-in effect, on tracks, a return and the master, under a tempo ramp so
// every block hands the processors a new transport. A thousand process()
// calls across a seek, many loop wraps and live mixer moves (bypass, send
// levels, pre/post) made between them must not allocate or free; the audio
// is then checked, so the zero comes from an engine that played.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "audio/engine/test_access.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/effects/builtin.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace {

using blokkily::realtime::require;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;
constexpr blokkily::Tick song_length = 30000;   // one tick per sample at 120 BPM
constexpr float source_level = 0.5F;

void dc_source(void* context, blokkily::StereoBlock track, std::uint64_t) noexcept {
    const float level = *static_cast<const float*>(context);
    for (auto& sample : track.left) sample += level;
    for (auto& sample : track.right) sample += level;
}

blokkily::EffectSlot slot(const char* format, const char* identifier) {
    return {{format, "", identifier, {}}, false, {}};
}

} // namespace

BLOKKILY_REALTIME_CASE(effects) {
    using namespace blokkily;
    using blokkily::realtime::require_no_allocations;

    Pattern pattern(song_length, 24000);
    for (Tick start = 0; start < song_length; start += 6000) {
        Trigger trigger;
        trigger.start = start;
        trigger.duration = 3000;
        trigger.musical_data = Note{60, 1.0F, 0.0F};
        (void)pattern.add(trigger);
    }
    Song song;
    song.patterns = {{"Effects", std::move(pattern)}};
    song.tempo.points = {{0, 120.0, true}, {song_length / 2, 150.0, false}};
    song.tracks = {Track{}, Track{}};
    song.clips = {{0, 0, 0, 1}};
    song.returns = {ReturnBus{}};
    // Track 0: the CLAP synth through the CLAP effect, the VST3 effect and
    // the synced delay. Track 1: a source through the EQ. Both send to the
    // return (reverb, compressor); the master has a compressor.
    song.tracks[0].inserts = {slot("CLAP", "dev.blokkily.test.effect"), slot("VST3", ""),
                              slot("Built-in", "delay")};
    song.tracks[1].inserts = {slot("Built-in", "eq3")};
    song.tracks[0].sends = {{0, -6.0, false}};
    song.tracks[1].sends = {{0, -3.0, true}};
    song.returns[0].inserts = {slot("Built-in", "reverb"), slot("Built-in", "compressor")};
    song.master_inserts = {slot("Built-in", "compressor")};

    std::string error;
    SongEngine engine;
    engine.set_processor(track_instrument(0),
                         ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test",
                                                    &error));
    engine.set_processor({BusKind::track, 0, 0},
                         ClapPluginInstance::create(BLOKKILY_TEST_CLAP_EFFECT_PATH,
                                                    "dev.blokkily.test.effect", &error));
    engine.set_processor({BusKind::track, 0, 1},
                         Vst3PluginInstance::create(BLOKKILY_TEST_VST3_EFFECT_PATH, 0, &error));
    engine.set_processor({BusKind::track, 0, 2}, create_builtin_effect("delay"));
    engine.set_processor({BusKind::track, 1, 0}, create_builtin_effect("eq3"));
    engine.set_processor({BusKind::ret, 0, 0}, create_builtin_effect("reverb"));
    engine.set_processor({BusKind::ret, 0, 1}, create_builtin_effect("compressor"));
    engine.set_processor({BusKind::master, 0, 0}, create_builtin_effect("compressor"));
    require(engine.processor({BusKind::track, 0, 0}) != nullptr &&
                engine.processor({BusKind::track, 0, 1}) != nullptr,
            "the effect fixtures must load through the production adapters: " + error);
    require(engine.prepare(song, 48000.0, block, 0, &error), "prepare: " + error);
    require(engine.output_latency() == 96, "the CLAP 64 and VST3 32 are compensated");
    float level = source_level;
    engine::TestAccess::set_test_source(engine, 1, &dc_source, &level);

    std::vector<float> left(calls * block, -1.0F);
    std::vector<float> right(calls * block, -1.0F);
    float return_heard = 0.0F;
    PluginEditEvent edit;
    engine.set_playing(true);
    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        // Live mixer moves between blocks, as the control thread makes them.
        if (call % 50 == 25) {
            song.tracks[0].inserts[call % 3].bypass = !song.tracks[0].inserts[call % 3].bypass;
            song.tracks[0].sends[0].level_db = call % 100 == 25 ? -12.0 : -3.0;
            song.tracks[1].sends[0].pre_fader = !song.tracks[1].sends[0].pre_fader;
            song.master_inserts[0].bypass = call % 100 == 25;
            engine.apply_mix(song);
        }
        if (call == 500) engine.seek(7777);
        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        {
            test::AllocationGuard guard;
            engine.process({out_left, out_right});
            total += guard.count();
        }
        return_heard = std::max(return_heard, engine.return_peak(0));
        while (engine.take_plugin_edit(edit)) {
        }
    }
    require_no_allocations(total, "effects: SongEngine::process with insert chains");
    require(blokkily::probe::rms(std::span<const float>(left).subspan(left.size() / 2)) > 1e-3,
            "the effects song still sounds after a thousand blocks");
    require(return_heard > 0.0F, "the return bus was fed by the sends");
}
