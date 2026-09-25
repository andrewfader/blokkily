// features/audio_reliability.feature: The review fixes keep the real-time
// rules.
//
// The production SongEngine::process() with what the Wave 2 fixes changed in
// it: the arrangement handoff (one atomic state word) taking recompiles that a
// second, control thread publishes while the callback runs; captured input
// stamped the output latency earlier (the CLAP effect's 64 samples) with the
// heard tick published after every block; and SongEngine::reset_processing(),
// which a bounce calls, resetting the CLAP synth, the CLAP effect, the
// built-in delay and reverb and every compensation line. Only the render
// thread is counted: not one armed process() or reset may allocate or free.
// The audio and the captured stamps are then checked, so the zeros come from
// an engine that played.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/effects/builtin.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>
#include <vector>

namespace {

using blokkily::realtime::require;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;

blokkily::Song recorded_song(blokkily::Tick shift) {
    using namespace blokkily;
    Pattern pattern(1920, 480);
    for (Tick start = shift; start < 1920; start += 240) {
        Trigger trigger;
        trigger.start = start;
        trigger.duration = 120;
        trigger.musical_data = Note{60, 1.0F, 0.0F};
        (void)pattern.add(trigger);
    }
    Song song;
    song.patterns = {{"Played", std::move(pattern)}, {"Take", Pattern(1920, 480)}};
    song.tracks = {Track{}, Track{}};
    song.tracks[1].input.armed = true;
    song.clips = {{0, 0, 0, 1}, {1, 1, 0, 1}};
    song.returns = {ReturnBus{}};
    song.tracks[0].sends = {{0, -6.0, false}};
    auto slot = [](const char* format, const char* identifier) {
        return EffectSlot{{format, "", identifier, {}}, false, {}};
    };
    song.tracks[0].inserts = {slot("CLAP", "dev.blokkily.test.effect"),
                              slot("Built-in", "delay")};
    song.returns[0].inserts = {slot("Built-in", "reverb")};
    return song;
}

} // namespace

BLOKKILY_REALTIME_CASE(wave2_fixes) {
    using namespace blokkily;
    using blokkily::realtime::require_no_allocations;

    auto song = recorded_song(0);
    std::string error;
    SongEngine engine;
    for (std::uint32_t track = 0; track < 2; ++track)
        engine.set_processor(track_instrument(track),
                             ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH,
                                                        "dev.blokkily.test", &error));
    engine.set_processor({BusKind::track, 0, 0},
                         ClapPluginInstance::create(BLOKKILY_TEST_CLAP_EFFECT_PATH,
                                                    "dev.blokkily.test.effect", &error));
    engine.set_processor({BusKind::track, 0, 1}, create_builtin_effect("delay"));
    engine.set_processor({BusKind::ret, 0, 0}, create_builtin_effect("reverb"));
    require(engine.has_instrument(0) && engine.has_instrument(1) &&
                engine.processor({BusKind::track, 0, 0}) != nullptr,
            "the CLAP fixtures must load through the production adapter: " + error);
    require(engine.prepare(song, 48000.0, block, 0, &error), "prepare: " + error);
    require(engine.output_latency() == 64, "the CLAP effect's 64 samples are the latency");

    // The control thread recompiles the whole time the callback runs,
    // shifting the arrangement by a tick each time.
    std::atomic<bool> stop{false};
    std::atomic<int> published{0};
    std::thread control([&] {
        Tick shift = 0;
        while (!stop.load(std::memory_order_acquire)) {
            shift = (shift + 1) % 120;
            if (engine.recompile(recorded_song(shift), 0, nullptr))
                published.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::yield();
        }
    });

    std::vector<float> left(calls * block, -1.0F);
    std::vector<float> right(calls * block, -1.0F);
    std::vector<CapturedEvent> captured;
    std::vector<std::uint64_t> struck_at;
    captured.reserve(4096);
    engine.set_recording(true);
    engine.set_playing(true);
    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        // Keys struck on screen every twenty blocks, released ten later.
        if (call % 20 == 5) {
            (void)engine.perform(1, {PluginEvent::Type::note_on, 0, 67, 1.0, 0.0});
            struck_at.push_back(engine.sample_position());
        }
        if (call % 20 == 15) (void)engine.perform(1, {PluginEvent::Type::note_off, 0, 67, 0, 0});
        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        {
            test::AllocationGuard guard;
            engine.process({out_left, out_right});
            // What a bounce does before and after it renders, halfway.
            if (call == calls / 2) engine.reset_processing();
            total += guard.count();
        }
        CapturedEvent event;
        while (engine.take_captured(event)) captured.push_back(event);
    }
    stop.store(true, std::memory_order_release);
    control.join();
    require_no_allocations(total, "wave2 fixes: SongEngine::process with the handoff, the "
                                  "latency-stamped capture and reset_processing");
    require(published.load() > 10, "recompiles really raced the callback: " +
                                       std::to_string(published.load()));
    require(probe::rms(std::span<const float>(left).subspan(left.size() / 2)) > 1e-3,
            "the song still sounds after the reset and a thousand blocks");

    // Every key-down was stamped 64 samples before the block it arrived in.
    std::size_t downs = 0;
    const auto song_samples = engine.song_samples();
    for (const auto& event : captured) {
        if (event.event.type != PluginEvent::Type::note_on) continue;
        require(downs < struck_at.size(), "no more key-downs than were struck");
        const auto arrived = struck_at[downs] % song_samples;
        const auto heard = (arrived + song_samples - 64) % song_samples;
        require(event.track == 1 && event.sample == heard,
                "key-down " + std::to_string(downs) + " stamped at " +
                    std::to_string(event.sample) + ", heard at " + std::to_string(heard));
        ++downs;
    }
    require(downs == struck_at.size(), "every key-down was captured");
}
