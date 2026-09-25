// features/audio_input.feature: Recording and monitoring audio input never
// makes the render callback allocate.
//
// Three tracks take device input through the production SongEngine::process
// with an input block: a stereo pair recorded and monitored, a mono input
// monitored while armed, and a track that records the same pair unmonitored.
// A thousand blocks are rendered while recording, across a seek and several
// loop wraps, first with a writer thread emptying the capture ring and then
// with nobody emptying it, so the ring fills and every later chunk takes the
// dropped path. Not one process() call may allocate or free, and the audio
// proves both the monitoring and the capture really ran.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/audio_input.hpp"
#include "blokkily/audio/sample_ring.hpp"
#include "blokkily/audio/song_engine.hpp"

#include <atomic>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

namespace {

using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;
constexpr std::size_t seek_before_call = 300;
constexpr std::size_t stop_reader_at_call = 500;
constexpr std::uint32_t channels = 4;

} // namespace

BLOKKILY_REALTIME_CASE(audio_input) {
    using namespace blokkily;

    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks.assign(3, Track{});
    song.clips = {{0, 0, 0, 1}};   // one bar: 48000 samples at 120 BPM
    song.tracks[0].input = {true, TrackInput::Source::audio, -1, 0, 2, TrackInput::Monitor::on};
    song.tracks[1].input = {true, TrackInput::Source::audio, -1, 2, 1,
                            TrackInput::Monitor::automatic};
    song.tracks[2].input = {true, TrackInput::Source::midi_and_audio, -1, 0, 2,
                            TrackInput::Monitor::off};

    SongEngine engine;
    std::string error;
    require(engine.prepare(song, 48000.0, block, 0, &error), "prepare: " + error);
    engine.set_audio_inputs(audio_input_routes(song, 0));
    require(calls * block > 2 * engine.song_samples(), "the run wraps the song at least twice");

    // A generous ring for the first half, drained by a writer thread; after
    // that nobody drains it and it must fill.
    SampleRing ring(block * 2 * 64);
    engine.connect_capture(&ring);
    std::atomic<bool> reading{true};
    std::atomic<std::uint64_t> popped{0};
    std::thread reader([&] {
        CaptureHeader header;
        std::vector<float> samples;
        while (reading.load(std::memory_order_acquire)) {
            while (ring.pop(header, samples)) popped.fetch_add(header.frames);
            std::this_thread::yield();
        }
    });

    // Every input channel carries its own steady level.
    std::vector<float> input(channels * block);
    for (std::uint32_t channel = 0; channel < channels; ++channel)
        for (std::size_t frame = 0; frame < block; ++frame)
            input[channel * block + frame] = 0.01F * static_cast<float>(channel + 1);
    const InputBlock device{input.data(), channels, block, block};

    std::vector<float> left(calls * block, -1.0F);
    std::vector<float> right(calls * block, -1.0F);
    engine.set_recording(true);
    engine.set_playing(true);
    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        if (call == seek_before_call) engine.seek(30000);
        if (call == stop_reader_at_call) {
            reading.store(false, std::memory_order_release);
            reader.join();
        }
        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        {
            test::AllocationGuard guard;
            engine.process({out_left, out_right}, device);
            total += guard.count();
        }
    }
    if (reader.joinable()) {
        reading.store(false, std::memory_order_release);
        reader.join();
    }
    engine.connect_capture(nullptr);
    require_no_allocations(total, "1000 SongEngine::process calls recording and monitoring input");

    // Monitored: track 0 hears inputs 1-2 (0.01, 0.02) and track 1 input 3
    // (0.03) on both sides, each through a centred strip; track 2 is not
    // monitored.
    const float centre = strip_gain(MixerStrip{}, false).left;
    const float expect_left = (0.01F + 0.03F) * centre;
    const float expect_right = (0.02F + 0.03F) * centre;
    require(std::abs(left.back() - expect_left) < 1e-6F &&
                std::abs(right.back() - expect_right) < 1e-6F,
            "the monitored inputs were heard: " + std::to_string(left.back()) + " / " +
                std::to_string(right.back()));
    // Captured: tracks 0 and 2 while the reader ran, then the ring filled.
    require(popped.load() >= 2 * stop_reader_at_call * block / 2,
            "the writer thread received what the recording tracks captured");
    require(ring.dropped_frames() > 0, "with nobody reading, the ring filled and dropped");
}
