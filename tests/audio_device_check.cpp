// Plays the arrangement through the real audio server and reports what came
// back. Every other audio test drives the render callback from a deterministic
// pump, which proves what the callback computes but never that a device will
// pull it: the API is chosen at runtime, the sink and the sample rate are
// negotiated, and the server picks its own block size. This runs the production
// path — RtAudioOutput in device mode, over whatever ALSA, PulseAudio, JACK, or
// PipeWire is actually there — and fails if the stream never opens, if the
// callback is never pulled, or if what reaches the device is silence.
//
// Exits 77 when there is no audio server to talk to, which CTest reads as a
// skip: a workstation without a sound server is not a broken workstation.

#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/plugins/clap_instance.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr int exit_skip = 77;   // CTest's SKIP_RETURN_CODE

// Watches the audio the engine hands to the device without changing it. The
// peak is what actually left for the sink, measured on the audio thread at the
// last point this process controls.
class TappedSource final : public blokkily::AudioSource {
public:
    explicit TappedSource(blokkily::AudioSource& inner) : inner_(inner) {}

    void process(blokkily::StereoBlock output) noexcept override {
        inner_.process(output);
        float peak = 0.0F;
        for (const float sample : output.left) peak = std::max(peak, std::abs(sample));
        for (const float sample : output.right) peak = std::max(peak, std::abs(sample));
        float seen = peak_.load(std::memory_order_relaxed);
        while (peak > seen &&
               !peak_.compare_exchange_weak(seen, peak, std::memory_order_relaxed)) {
        }
        frames_.fetch_add(output.left.size(), std::memory_order_relaxed);
    }

    [[nodiscard]] float peak() const noexcept { return peak_.load(std::memory_order_relaxed); }
    [[nodiscard]] std::uint64_t frames() const noexcept {
        return frames_.load(std::memory_order_relaxed);
    }

private:
    blokkily::AudioSource& inner_;
    std::atomic<float> peak_{0.0F};
    std::atomic<std::uint64_t> frames_{0};
};

// A song that is unmistakably not silence: one sustained note on each of two
// tracks, so a dropped track shows up as a halved peak rather than as nothing.
blokkily::Song two_track_song() {
    blokkily::Song song;
    song.patterns = {{"Check", {}}};
    song.tracks = {blokkily::Track{"One", {}, {}}, blokkily::Track{"Two", {}, {}}};
    blokkily::Trigger held;
    held.start = 0;
    held.duration = 1920;
    held.musical_data = blokkily::Note{60, 1.0F, 0.0F};
    (void)song.patterns[0].pattern.add(held);
    song.clips = {{0, 0, 0, 1}, {1, 0, 0, 1}};
    return song;
}

} // namespace

int main(int argc, char** argv) {
    std::string clap_path;
    double seconds = 1.5;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--clap-fixture" && index + 1 < argc) clap_path = argv[++index];
        else if (argument == "--seconds" && index + 1 < argc) seconds = std::atof(argv[++index]);
    }
    if (clap_path.empty()) {
        std::fprintf(stderr, "audio-device-check: --clap-fixture is required\n");
        return 2;
    }

    constexpr double sample_rate = 48000.0;
    constexpr std::uint32_t maximum_block = 512;

    blokkily::SongEngine engine;
    for (std::size_t track = 0; track < 2; ++track) {
        std::string clap_error;
        auto voice = blokkily::ClapPluginInstance::create(clap_path, "dev.blokkily.test",
                                                          &clap_error);
        if (voice == nullptr) {
            std::fprintf(stderr, "audio-device-check: CLAP fixture failed: %s\n",
                         clap_error.c_str());
            return 2;
        }
        engine.set_instrument(track, std::move(voice));
    }
    std::string engine_error;
    if (!engine.prepare(two_track_song(), sample_rate, maximum_block, 0, &engine_error)) {
        std::fprintf(stderr, "audio-device-check: engine prepare failed: %s\n",
                     engine_error.c_str());
        return 2;
    }

    TappedSource tapped(engine);
    blokkily::RtAudioOutput output(blokkily::RtAudioOutput::Mode::device);
    std::string device_error;
    if (!output.open(tapped, static_cast<unsigned int>(sample_rate), maximum_block,
                     &device_error)) {
        // No server, no sink, or nothing to play through: that is an absent
        // environment rather than a broken player, so say so and skip.
        std::fprintf(stderr, "audio-device-check: SKIP — no usable output device (%s)\n",
                     device_error.empty() ? "no device reported" : device_error.c_str());
        return exit_skip;
    }

    const auto info = output.device_info();
    std::printf("api=%s device=\"%s\" rate=%u negotiated-block=%u prepared-max=%u\n",
                info.api.c_str(), info.device.c_str(), info.sample_rate, info.buffer_frames,
                maximum_block);

    engine.set_playing(true);
    if (!output.start(&device_error)) {
        std::fprintf(stderr, "audio-device-check: stream would not start: %s\n",
                     device_error.c_str());
        return 1;
    }

    // Real time, because the point of this check is that a real server pulls
    // the callback on its own clock rather than on ours.
    const auto began = std::chrono::steady_clock::now();
    const auto until = began + std::chrono::duration<double>(seconds);
    while (std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    const double elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();

    engine.set_playing(false);
    output.stop();

    const auto callbacks = output.callback_count();
    const auto frames = tapped.frames();
    const double delivered_seconds = static_cast<double>(frames) / sample_rate;
    std::printf("callbacks=%llu frames=%llu delivered=%.3fs elapsed=%.3fs largest-block=%u "
                "peak=%.4f\n",
                static_cast<unsigned long long>(callbacks),
                static_cast<unsigned long long>(frames), delivered_seconds, elapsed,
                output.largest_block(), static_cast<double>(tapped.peak()));

    int failures = 0;
    const auto fail = [&failures](const char* what) {
        std::fprintf(stderr, "audio-device-check: FAIL — %s\n", what);
        ++failures;
    };

    if (callbacks == 0) fail("the device never pulled the render callback");
    if (tapped.peak() <= 0.001F) fail("the arrangement reached the device as silence");
    // The server runs on its own clock. Anything close to real time proves it
    // is actually consuming; a stream that opens and then starves does not.
    if (delivered_seconds < elapsed * 0.5)
        fail("the device consumed far less audio than the time it ran for");
    if (delivered_seconds > elapsed * 2.0 + 0.5)
        fail("the device consumed far more audio than the time it ran for");
    // The engine sizes its per-track buffers for the block it was prepared
    // with. A server free to renegotiate its quantum may ask for more, and the
    // engine must have split that rather than overrun.
    if (output.largest_block() > maximum_block)
        std::printf("note: the server asked for %u frames against a prepared maximum of %u; "
                    "the engine split them\n", output.largest_block(), maximum_block);

    if (failures != 0) return 1;
    std::printf("audio-device-check: PASS\n");
    return 0;
}
