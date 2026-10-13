// features/midi_timing.feature: placing played input by its arrival time costs
// the audio thread nothing.
//
// A real CLAP instrument loaded through the production adapter is played
// from a deterministic keyboard whose messages are stamped part way through
// each period, on a clock the test advances. The device asks for 1024-sample
// blocks from an engine that renders 256-sample chunks, so the input is
// placed, sorted and handed out across chunks, and recording is armed, so it
// is captured at its sample too. A thousand process() calls may not allocate
// or free, and every key must be heard on the sample it was placed on.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/midi/midi_input.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace {

using blokkily::realtime::require;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 1024;
constexpr std::uint64_t period = 1'024'000;   // 1000 ns a sample
std::atomic<std::uint64_t> now{1'000'000'000};
std::uint64_t test_clock() noexcept { return now.load(std::memory_order_relaxed); }

// Where in its period call c's key goes down: a different sample each time.
std::size_t pressed_at(std::size_t call) { return (call * 97) % 900; }

} // namespace

BLOKKILY_REALTIME_CASE(midi_timing) {
    using namespace blokkily;
    using blokkily::realtime::require_no_allocations;

    Song song;
    song.tracks = {Track{}};
    std::string error;
    SongEngine engine;
    engine.set_instrument(0, ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH,
                                                        "dev.blokkily.test", &error));
    require(engine.has_instrument(0), "the CLAP fixture must load: " + error);
    require(engine.prepare(song, 48000.0, 256, 0, &error), "prepare: " + error);
    MidiInput keyboard(MidiInput::Mode::deterministic);
    require(keyboard.open(std::size_t{0}, &error), "open the deterministic input: " + error);
    keyboard.set_track(0);
    engine.connect_input(&keyboard.queue());
    engine.set_input_clock(&test_clock);
    engine.set_recording(true);
    engine.set_playing(true);

    std::vector<float> left(calls * block, -1.0F);
    std::vector<float> right(calls * block, -1.0F);
    const std::array<std::uint8_t, 3> key_down{0x90, 64, 100};
    const std::array<std::uint8_t, 3> key_up{0x80, 64, 0};
    std::size_t captured = 0;
    CapturedEvent event;

    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        // The keyboard's side, outside the armed region: on even periods a
        // key goes down part way through, on odd ones it comes up at their
        // very end.
        const auto begun = now.load();
        if (call % 2 == 0)
            require(keyboard.inject(key_down, begun + pressed_at(call) * 1000), "key down");
        else
            require(keyboard.inject(key_up, begun + period - 1000), "key up");
        now += period;
        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        {
            test::AllocationGuard guard;
            engine.process({out_left, out_right});
            total += guard.count();
        }
        while (engine.take_captured(event)) ++captured;
    }
    engine.set_input_clock(nullptr);
    require_no_allocations(total, "1000 SongEngine::process calls with stamped input");

    // Each key is heard from the sample it was placed on, in the block after
    // the period it arrived in.
    const std::span<const float> heard{left};
    for (std::size_t call = 0; call < calls; call += 2) {
        const auto rendered = heard.subspan(call * block, block);
        require(probe::first_nonzero(rendered, 0.1F) == std::optional<std::size_t>{pressed_at(call)},
                "the key placed in call " + std::to_string(call) + " must start on sample " +
                    std::to_string(pressed_at(call)));
    }
    require(captured == calls, "every key press and release must be captured");
}
