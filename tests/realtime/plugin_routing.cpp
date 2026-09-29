// features/sidechain_and_multiout.feature: Plugin sidechains and instrument
// outputs keep the real-time rules.
//
// Wave 5.2 through the production adapters in SongEngine::process(): the CLAP
// and VST3 synths each have their aux output broken out to a track of its
// own, and each of those channels runs an effect fixture keyed from the
// other synth's track (the VST3 effect on the CLAP synth's channel, keyed by
// the VST3 synth; the CLAP effect on the VST3 synth's channel, keyed by the
// CLAP synth). A thousand process() calls across loop wraps, a seek, and a
// recompile that unroutes one output and moves a key must not allocate or
// free. The audio is checked afterwards: the channels played.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <string>
#include <vector>

namespace {

using namespace blokkily;
using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;
constexpr Tick song_length = 30000; // one tick per sample at 120 BPM

EffectSlot effect(const char* format, const char* identifier, std::uint32_t key) {
    EffectSlot slot{{format, "", identifier, {}}, false, {}};
    slot.sidechain = key;
    return slot;
}

} // namespace

BLOKKILY_REALTIME_CASE(plugin_routing) {
    Pattern pattern(song_length, 24000);
    for (Tick start = 0; start < song_length; start += 6000) {
        Trigger trigger;
        trigger.start = start;
        trigger.duration = 3000;
        trigger.musical_data = Note{60, 1.0F, 0.0F};
        (void)pattern.add(trigger);
    }
    Song song;
    song.patterns = {{"P", std::move(pattern)}};
    song.tracks = {Track{}, Track{}, Track{}, Track{}};
    song.tracks[0].instrument = {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", {}};
    song.tracks[2].instrument = {"VST3", BLOKKILY_TEST_VST3_PATH, "", {}};
    song.tracks[1].source = InstrumentOutput{0, 1};
    song.tracks[3].source = InstrumentOutput{2, 1};
    song.tracks[1].inserts = {effect("VST3", "", 2)};
    song.tracks[3].inserts = {effect("CLAP", "dev.blokkily.test.effect", 0)};
    // The synths are muted: their aux outputs are heard through the
    // channels, and their keys are taken before the fader. The CLAP synth's
    // channel is hard left, the VST3 synth's hard right.
    song.tracks[0].mix.mute = true;
    song.tracks[2].mix.mute = true;
    song.tracks[1].mix.pan = -1.0;
    song.tracks[3].mix.pan = 1.0;
    song.clips = {{0, 0, 0, 1}, {2, 0, 0, 1}};
    std::string why;
    require(song.consistent(&why), "the song is valid: " + why);

    std::string error;
    SongEngine engine;
    engine.set_processor(track_instrument(0),
                         ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test",
                                                    &error));
    engine.set_processor(track_instrument(2),
                         Vst3PluginInstance::create(BLOKKILY_TEST_VST3_PATH, 0, &error));
    engine.set_processor({BusKind::track, 1, 0},
                         Vst3PluginInstance::create(BLOKKILY_TEST_VST3_EFFECT_PATH, 0, &error));
    engine.set_processor({BusKind::track, 3, 0},
                         ClapPluginInstance::create(BLOKKILY_TEST_CLAP_EFFECT_PATH,
                                                    "dev.blokkily.test.effect", &error));
    require(engine.has_instrument(0) && engine.has_instrument(2) &&
                engine.processor({BusKind::track, 1, 0}) != nullptr &&
                engine.processor({BusKind::track, 3, 0}) != nullptr,
            "the fixtures load through the production adapters: " + error);
    require(engine.prepare(song, 48000.0, block, 0, &error), "prepare: " + error);
    auto moved = song;
    moved.tracks[3].source.reset();
    moved.tracks[1].inserts[0].sidechain = 0;

    std::vector<float> left(calls * block, 0.0F);
    std::vector<float> right(calls * block, 0.0F);
    engine.set_playing(true);
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
    require_no_allocations(total, "1000 SongEngine::process calls with plugin keys and "
                                  "instrument outputs");
    // While a note sounds (the first 3000 samples of every 6000, 64 late on
    // every track) both channels play: on the left the CLAP aux (-0.125)
    // through the VST3 effect (x -0.25) ducked by the VST3 synth's 0.25 key
    // (x 0.75); on the right the VST3 aux (-0.125) through the CLAP effect
    // (x 0.25) ducked by the CLAP synth's key (x 0.75).
    const float expected = 0.125F * 0.25F * 0.75F;
    require(std::abs(left[7500] - expected) < 1e-5F && std::abs(right[7500] + expected) < 1e-5F,
            "both channels play their ducked aux outputs: " + std::to_string(left[7500]) +
                " / " + std::to_string(right[7500]));
    // After the recompile the VST3 synth's output has no channel: the right
    // side is silent while the left still plays.
    const auto tail = [&](const std::vector<float>& side) {
        return probe::peak(std::span<const float>(side).last(50 * block));
    };
    require(tail(right) == 0.0F && tail(left) > 0.02F,
            "the unrouted output is gone and the routed one plays: " + std::to_string(tail(right)) +
                " / " + std::to_string(tail(left)));
}
