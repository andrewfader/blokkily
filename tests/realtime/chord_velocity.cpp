// features/chord_velocity.feature: Each voice sounds as hard and as long as it
// was played — without the render callback allocating.
//
// A chord whose three voices carry their own velocities and lengths plays for
// a hundred loops through SongEngine::process and the real CLAP fixture in its
// velocity mode (parameter 2, turned on by a lock on the step). Not one call
// may allocate or free, and the rendered plateaus prove the voices really
// played as written: level × the sum of the voices still held.

#include "realtime_case.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <cmath>
#include <string>
#include <vector>

BLOKKILY_REALTIME_CASE(chord_velocity) {
    using namespace blokkily;
    using blokkily::realtime::require;
    using blokkily::realtime::require_no_allocations;

    constexpr std::size_t block = 256;
    constexpr std::size_t loops = 100;
    constexpr Tick length = 1024;   // one tick per sample at 120 BPM and 48 kHz

    Pattern pattern(length, 24000);
    Trigger step;
    step.start = 64;
    step.duration = 600;
    Chord chord{60, {0, 4, 7}, 0, 0, {}};
    chord.velocities = {0.6F, 0.9F, 0.2F};
    chord.durations = {600, 400, 200};
    step.musical_data = chord;
    step.locks = {{"velocity-mode", 2, 1.0, ParameterLock::Kind::automation}};
    (void)pattern.add(step);
    Song song;
    song.patterns = {{"Chord", std::move(pattern)}};
    song.tracks = {Track{}};
    song.tracks[0].mix.pan = -1.0;
    song.clips = {{0, 0, 0, 1}};

    std::string error;
    SongEngine engine;
    engine.set_instrument(0, ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH,
                                                        "dev.blokkily.test", &error));
    require(engine.has_instrument(0), "the CLAP fixture must load: " + error);
    require(engine.prepare(song, 120.0, 48000.0, block, 0, &error), "prepare: " + error);
    engine.set_playing(true);

    const std::size_t frames = loops * static_cast<std::size_t>(length);
    std::vector<float> left(frames, -1.0F);
    std::vector<float> right(frames, -1.0F);
    test::AllocationCount total;
    for (std::size_t at = 0; at < frames; at += block) {
        test::AllocationGuard guard;
        engine.process({std::span<float>{left.data() + at, block},
                        std::span<float>{right.data() + at, block}});
        total += guard.count();
    }
    require_no_allocations(total, "SongEngine::process playing a chord with per-voice velocity");

    const auto expected = [](Tick position) {
        float held = 0.0F;
        if (position >= 64 && position < 264) held += 0.2F;
        if (position >= 64 && position < 464) held += 0.9F;
        if (position >= 64 && position < 664) held += 0.6F;
        return 0.25F * held;
    };
    for (std::size_t sample = 0; sample < frames; ++sample) {
        const auto position = static_cast<Tick>(sample % static_cast<std::size_t>(length));
        require(std::abs(left[sample] - expected(position)) < 1e-5F,
                "chord plateau wrong at sample " + std::to_string(sample) + ": " +
                    std::to_string(left[sample]) + " vs " + std::to_string(expected(position)));
    }
}
