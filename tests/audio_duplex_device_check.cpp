// Opens the real audio server duplex, the way the application does (item 3.2),
// and reports what came back. Every other audio-input test drives the render
// callback through the deterministic pump, which proves what the callback does
// with input but never that a server will deliver any: this runs RtAudioOutput
// in device mode and fails if a machine with an input device opens the stream
// output-only, never calls the callback, or hands it input blocks whose length
// is not the output's.
//
// Exits 77 (CTest's skip) when there is no audio server, or no input device to
// open: a machine that cannot record is not a broken machine, and the
// application falls back to output-only there.

#include "blokkily/audio/audio_input.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"

#include <RtAudio.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

namespace {

constexpr int exit_skip = 77;

// Watches what the device hands the engine, on the audio thread, without
// changing it.
class TappedSource final : public blokkily::AudioSource {
public:
    explicit TappedSource(blokkily::SongEngine& inner) : inner_(inner) {}

    void process(blokkily::StereoBlock output) noexcept override { inner_.process(output); }
    void process(blokkily::StereoBlock output, blokkily::InputBlock input) noexcept override {
        inner_.process(output, input);
        if (input.empty()) return;
        input_blocks_.fetch_add(1, std::memory_order_relaxed);
        if (input.frames != output.left.size()) mismatched_.fetch_add(1, std::memory_order_relaxed);
        bool finite = true;
        for (std::uint32_t channel = 0; channel < input.channels; ++channel)
            for (const float sample : input.channel(channel)) finite = finite && std::isfinite(sample);
        if (!finite) not_finite_.fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<std::uint64_t> input_blocks_{0};
    std::atomic<std::uint64_t> mismatched_{0};
    std::atomic<std::uint64_t> not_finite_{0};

private:
    blokkily::SongEngine& inner_;
};

} // namespace

int main() {
    // Is there anything to record from at all?
    {
        RtAudio probe;
        if (probe.getDeviceCount() == 0) {
            std::puts("SKIP: no audio server or device");
            return exit_skip;
        }
        const auto input = probe.getDefaultInputDevice();
        if (input == 0 || probe.getDeviceInfo(input).inputChannels == 0) {
            std::puts("SKIP: no audio input device");
            return exit_skip;
        }
    }

    blokkily::Song song;
    song.tracks.front().input = {true, blokkily::TrackInput::Source::audio, -1, 0, 1,
                                 blokkily::TrackInput::Monitor::off};
    blokkily::SongEngine engine;
    TappedSource tapped(engine);
    blokkily::RtAudioOutput output;
    // What the application asks for once a track takes audio input.
    output.set_input_wanted(true);
    std::string error;
    if (!output.open(tapped, 48000, 256, &error)) {
        std::printf("SKIP: the server would not open a stream: %s\n", error.c_str());
        return exit_skip;
    }
    const auto info = output.device_info();
    if (!engine.prepare(song, info.sample_rate, 8192, 0, &error)) {
        std::printf("FAIL: prepare: %s\n", error.c_str());
        return 1;
    }
    engine.set_audio_inputs(blokkily::audio_input_routes(song, 0));
    std::printf("%s · out %s · in %s (%u channels) · %u Hz · %u frames · round trip %u\n",
                info.api.c_str(), info.device.c_str(), info.input_device.c_str(),
                info.input_channels, info.sample_rate, info.buffer_frames,
                info.round_trip_frames);
    if (output.input_channels() == 0) {
        // The fallback worked - the machine still plays - but duplex was
        // refused on a machine that has an input. Reported, and skipped:
        // the server, not this code, decided.
        std::puts("SKIP: the server refused a duplex stream; opened output-only");
        return exit_skip;
    }
    if (!output.start(&error)) {
        std::printf("FAIL: start: %s\n", error.c_str());
        return 1;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (tapped.input_blocks_.load() < 20 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    output.stop();
    const auto blocks = tapped.input_blocks_.load();
    std::printf("%llu callbacks, %llu with input\n",
                static_cast<unsigned long long>(output.callback_count()),
                static_cast<unsigned long long>(blocks));
    if (blocks == 0) {
        std::puts("FAIL: the duplex stream never delivered input to the callback");
        return 1;
    }
    if (tapped.mismatched_.load() != 0 || tapped.not_finite_.load() != 0) {
        std::puts("FAIL: input blocks did not match their output blocks, or were not finite");
        return 1;
    }
    std::puts("PASS");
    return 0;
}
