#pragma once

#include "blokkily/audio/audio_source.hpp"
#include "blokkily/audio/event_timeline.hpp"
#include "blokkily/model/pattern.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace blokkily {

class RealtimePlayback final : public AudioSource {
public:
    explicit RealtimePlayback(std::unique_ptr<PluginInstance> instrument);
    bool prepare(const Pattern& pattern, double bpm, double sample_rate,
                 std::uint32_t maximum_block_size, std::uint64_t seed = 0);
    void set_playing(bool playing) noexcept { playing_.store(playing, std::memory_order_release); }
    [[nodiscard]] bool is_playing() const noexcept {
        return playing_.load(std::memory_order_acquire);
    }
    void rewind() noexcept { sample_position_ = 0; }
    void process(StereoBlock output) noexcept override;
    [[nodiscard]] std::uint64_t sample_position() const noexcept { return sample_position_; }
    std::vector<std::byte> save_instrument_state();
    bool load_instrument_state(std::span<const std::byte> state);

private:
    void process_chunk(StereoBlock output, std::uint64_t loop_position) noexcept;

    std::unique_ptr<PluginInstance> instrument_;
    std::vector<TimedPluginEvent> events_;
    std::uint64_t loop_samples_ = 0;
    std::uint64_t sample_position_ = 0;
    std::uint32_t maximum_block_ = 0;
    std::atomic<bool> playing_{false};
};

class RtAudioOutput {
public:
    enum class Mode { device, deterministic };
    // What the audio server actually gave us. A device is negotiated, not
    // chosen: the API, the sink, the rate, and the block size can all differ
    // from what was asked for, and a player that cannot say which it got
    // cannot be diagnosed when it is silent.
    struct DeviceInfo {
        std::string api;
        std::string device;
        unsigned int sample_rate = 0;
        unsigned int buffer_frames = 0;
    };
    // A deterministic output can stand in for a server that negotiates a rate
    // other than the one asked for: `negotiated_rate`, when non-zero, is what
    // open() reports regardless of the request, the way a device locked to
    // 44.1 kHz answers a request for 48 kHz.
    explicit RtAudioOutput(Mode mode = Mode::device, unsigned int negotiated_rate = 0);
    ~RtAudioOutput();
    RtAudioOutput(const RtAudioOutput&) = delete;
    RtAudioOutput& operator=(const RtAudioOutput&) = delete;

    bool open(AudioSource& source, unsigned int sample_rate = 48000,
              unsigned int requested_frames = 256, std::string* error = nullptr);
    bool start(std::string* error = nullptr);
    void stop() noexcept;
    void rebind(AudioSource& source) noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] bool is_running() const noexcept;
    // Deterministic hosts drive the very same callback as RtAudio. The buffer
    // is non-interleaved stereo: all left frames followed by all right frames.
    bool pump(std::span<float> stereo) noexcept;
    // Valid once open() has succeeded on a real device; empty in deterministic
    // mode, which has no device to describe.
    [[nodiscard]] DeviceInfo device_info() const;
    // How many times the server has pulled the callback, and the largest block
    // it asked for. The engine is prepared for a maximum block; the server may
    // renegotiate its quantum at any time, so what it actually asked for is
    // worth knowing rather than assuming.
    [[nodiscard]] std::uint64_t callback_count() const noexcept;
    [[nodiscard]] unsigned int largest_block() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace blokkily
