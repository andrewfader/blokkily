// features/sampler.feature: The sampler plays without the render callback
// allocating.
//
// SamplerInstrument::process with every voice busy (64 notes, then more to
// force stealing into the fade slots), chokes, parameter values and
// modulations, and a kit swapped in between blocks while voices still hold
// the old kit; then the same sampler as a track instrument of the production
// SongEngine, playing live notes, with a state pushed through
// update_processor_state while it runs. Not one process() call may allocate
// or free, and the audio proves the voices really played.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/instruments/sampler.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

constexpr std::size_t block = 256;
const std::filesystem::path fixtures = BLOKKILY_AUDIO_FIXTURES;

blokkily::SamplerProgram looped_program(int choke_group) {
    blokkily::SamplerInstrument maker;
    std::string error;
    auto zone = maker.make_zone(fixtures / "loop480_48k_pcm16.wav", &error);
    require(zone.has_value(), "the loop fixture must decode: " + error);
    zone->choke_group = 0;
    blokkily::SamplerProgram program;
    program.polyphony = blokkily::sampler_max_voices;
    program.zones = {*zone};
    // Keys 20 and 21 choke each other.
    auto choked = *zone;
    choked.low_key = choked.high_key = 20;
    choked.choke_group = choke_group;
    auto choking = choked;
    choking.low_key = choking.high_key = 21;
    program.zones[0].low_key = 22;
    program.zones.push_back(choked);
    program.zones.push_back(choking);
    return program;
}

} // namespace

BLOKKILY_REALTIME_CASE(sampler_voices) {
    using namespace blokkily;

    auto sampler = std::make_unique<SamplerInstrument>();
    require(sampler->activate(48000.0, 1, block), "activate");
    require(sampler->set_program(looped_program(1)), "the program must load");
    const auto swapped = serialize_sampler(looped_program(2));

    std::vector<float> left(block), right(block);
    std::vector<PluginEvent> events;
    events.reserve(128);
    test::AllocationCount total;
    double energy = 0.0;
    for (int call = 0; call < 200; ++call) {
        events.clear();
        if (call == 0 || call == 50) {
            // Every voice busy, and then the same again to steal all of them.
            for (int key = 22; key < 22 + sampler_max_voices; ++key)
                events.push_back({PluginEvent::Type::note_on,
                                  static_cast<std::uint32_t>(key % block), key, 0.5,
                                  call == 50 ? 25.0 : 0.0});
        }
        if (call % 10 == 3) {
            events.push_back({PluginEvent::Type::note_on, 7, 20, 0.8});
            events.push_back({PluginEvent::Type::note_on, 90, 21, 0.8});
            events.push_back({PluginEvent::Type::parameter_value, 100,
                              sampler_parameter::gain, 0.6 + 0.01 * (call % 7)});
            events.push_back({PluginEvent::Type::parameter_modulation, 120,
                              sampler_parameter::tune, 0.01 * (call % 5)});
        }
        if (call == 150)
            for (int key = 0; key < 128; ++key)
                events.push_back({PluginEvent::Type::note_off, 200, key, 0.0});
        if (call == 100) {
            // The control thread swaps the kit between blocks: outside the
            // guard, as it would be on its own thread.
            require(sampler->load_state(swapped), "the kit swap must load");
        }
        test::AllocationGuard guard;
        sampler->process({left, right}, events);
        total += guard.count();
        if (call > 0 && call < 150) energy += probe::rms(left);
    }
    require_no_allocations(total, "SamplerInstrument::process with 64 voices, steals, chokes, "
                                  "parameters and a kit swap");
    require(energy / 149.0 > 0.05, "the voices must actually sound");
    sampler->collect();
    require(sampler->kit_count() == 1, "the old kit is freed once its voices have ended");

    // The same sampler as a track instrument of the production engine.
    Song song;
    song.patterns = {{"Empty", Pattern(1920, 480)}};
    song.tracks = {Track{}};
    SongEngine engine;
    engine.set_instrument(0, std::move(sampler));
    std::string error;
    require(engine.prepare(song, 48000.0, block, 0, &error), "prepare: " + error);
    std::vector<float> out_left(block), out_right(block);
    double engine_energy = 0.0;
    total = {};
    for (int call = 0; call < 200; ++call) {
        if (call == 0)
            for (int key = 22; key < 22 + 40; ++key)
                (void)engine.play_live(0, {PluginEvent::Type::note_on, 0, key, 0.3});
        if (call == 100)
            require(engine.update_processor_state(track_instrument(0), swapped),
                    "the running sampler takes a new state");
        if (call == 101) (void)engine.play_live(0, {PluginEvent::Type::note_on, 0, 30, 0.3});
        test::AllocationGuard guard;
        engine.process({out_left, out_right});
        total += guard.count();
        engine_energy += probe::rms(out_left);
    }
    require_no_allocations(total, "SongEngine::process driving the sampler");
    require(engine_energy / 200.0 > 0.05, "the engine must render the sampler");
}
