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
    std::atomic<bool> playing_{false};
};

class RtAudioOutput {
public:
    RtAudioOutput();
    ~RtAudioOutput();
    RtAudioOutput(const RtAudioOutput&) = delete;
    RtAudioOutput& operator=(const RtAudioOutput&) = delete;

    bool open(AudioSource& source, unsigned int sample_rate = 48000,
              unsigned int requested_frames = 256, std::string* error = nullptr);
    bool start(std::string* error = nullptr);
    void stop() noexcept;
    void rebind(AudioSource& source) noexcept;
    [[nodiscard]] bool is_open() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace blokkily
