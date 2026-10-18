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
        // The input side of a duplex stream (item 3.2): its device and how
        // many channels it delivers. Zero when the stream is output only,
        // because the machine has no input or the duplex open was refused.
        std::string input_device;
        unsigned int input_channels = 0;
        // What the server reports its round trip (input plus output) to be,
        // in frames. Often wrong, which Song::record_offset_samples corrects.
        unsigned int round_trip_frames = 0;
    };
    // A deterministic output can stand in for a server that negotiates a rate
    // other than the one asked for: `negotiated_rate`, when non-zero, is what
    // open() reports regardless of the request, the way a device locked to
    // 44.1 kHz answers a request for 48 kHz.
    explicit RtAudioOutput(Mode mode = Mode::device, unsigned int negotiated_rate = 0);
    // A device on one audio system (features/audio_systems.feature): "alsa",
    // "pulse", "jack" (JACK, or PipeWire through its JACK interface) ...; empty
    // lets RtAudio choose, as before. A system this build or machine does
    // not have makes open() fail and say so.
    RtAudioOutput(Mode mode, const std::string& api);
    struct AudioSystem {
        std::string name;    // what RtAudioOutput takes: "alsa", "jack", ...
        std::string display; // what a person reads: "ALSA", "Jack", ...
    };
    // The audio systems this build can open, in RtAudio's order of
    // preference.
    [[nodiscard]] static std::vector<AudioSystem> audio_systems();
    // A device one audio system offers (features/audio_settings.feature): its
    // name, how many channels it plays and records, and the rates it takes.
    struct AudioDevice {
        std::string name;
        unsigned int outputs = 0;
        unsigned int inputs = 0;
        std::vector<unsigned int> sample_rates;
        unsigned int preferred_rate = 0;
        bool default_output = false;
        bool default_input = false;
    };
    // The devices on `api` ("" for RtAudio's own choice). Empty for JACK,
    // whose server is patched rather than its devices chosen, and for a
    // system this build does not have.
    [[nodiscard]] static std::vector<AudioDevice> devices(const std::string& api);
    // Which devices the next open() plays and records through, by name.
    // Empty, or a device no longer there, is the system's default. A
    // deterministic device and JACK ignore it.
    void set_devices(std::string output, std::string input);
    // How it was made: a real device, or the deterministic pump.
    [[nodiscard]] Mode mode() const noexcept;
    ~RtAudioOutput();
    RtAudioOutput(const RtAudioOutput&) = delete;
    RtAudioOutput& operator=(const RtAudioOutput&) = delete;

    // Opens the default output, and - when input is wanted (set_input_wanted)
    // - the default input with it, as one duplex stream, when the machine has
    // one. A duplex stream the server refuses (mismatched rates, a busy
    // capture device) falls back to output only, so a machine that cannot
    // record still plays. Input is opt-in because a duplex stream costs more
    // than one that only plays: on some servers it underruns where an output
    // stream would not, and a song that records nothing should not pay that.
    bool open(AudioSource& source, unsigned int sample_rate = 48000,
              unsigned int requested_frames = 256, std::string* error = nullptr);
    bool start(std::string* error = nullptr);
    void stop() noexcept;
    void rebind(AudioSource& source) noexcept;
    [[nodiscard]] bool is_open() const noexcept;
    // Whether the next open() asks for the inputs too (device mode).
    void set_input_wanted(bool wanted) noexcept;
    // Whether the open stream was opened asking for input as `wanted` says:
    // false means it has to be closed and opened again to follow. Always true
    // for a deterministic device, whose inputs are modelled, and when closed.
    [[nodiscard]] bool opened_for_input(bool wanted) const noexcept;
    // Stops and closes the stream, so it can be opened again. Device mode.
    void close() noexcept;
    [[nodiscard]] bool is_running() const noexcept;
    // Deterministic hosts drive the very same callback as RtAudio. The buffer
    // is non-interleaved stereo: all left frames followed by all right frames.
    bool pump(std::span<float> stereo) noexcept;
    // Deterministic mode (item 3.2): the device's inputs. There are `channels`
    // of them, and the device reports `round_trip` frames of latency. With
    // `loopback`, the outputs are cabled back to the inputs with exactly that
    // round trip: input channel c hears output channel c % 2 as it was played
    // `round_trip` frames earlier. A block is then pumped in pieces no longer
    // than the round trip, because a cable cannot deliver what has not been
    // played yet. No inputs (the default) is an output-only device. Only while
    // the output is not being pumped.
    void model_input(unsigned int channels, std::uint32_t round_trip = 0, bool loopback = false);
    // The same callback with input: `injected` is added to what each input
    // channel hears, non-interleaved (all of channel 0, then channel 1, ...),
    // and must be channels x frames long, or empty for none.
    bool pump(std::span<float> stereo, std::span<const float> injected);
    // How many input channels the open stream delivers; 0 for output only.
    [[nodiscard]] unsigned int input_channels() const noexcept;
    // The round trip the device reports, in frames (see DeviceInfo).
    [[nodiscard]] std::uint32_t round_trip_latency() const noexcept;
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
