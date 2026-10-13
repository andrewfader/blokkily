#pragma once

#include "blokkily/midi/midi_output.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <atomic>
#include <cstdint>
#include <memory>

namespace blokkily {

// The slot format of a track whose instrument is outboard gear: its
// identifier names the MIDI output port, its state is one byte, the channel
// (0-15).
inline constexpr const char* midi_out_format = "MIDI Out";

// A track instrument that sounds nothing itself: what the track plays (its
// notes, and the controller movements of its patterns and of a keyboard
// played into it) goes to a MIDI output port on the track's channel, each
// message due when its sample reaches the speakers. Parameter locks and
// automation have nothing to move; microtonal retuning is not sent.
//
// It needs the output to outlive it. When it goes, it lets go of every note
// it struck on its channel (All Notes Off).
class MidiOutInstrument final : public PluginInstance {
public:
    MidiOutInstrument(std::shared_ptr<MidiOutput> output, std::uint8_t channel);
    ~MidiOutInstrument() override;

    bool activate(double sample_rate, std::uint32_t min_frames,
                  std::uint32_t max_frames) override;
    void process(StereoBlock audio, std::span<const PluginEvent> events) noexcept override;
    std::vector<std::byte> save_state() override;
    bool load_state(std::span<const std::byte> state) override;
    [[nodiscard]] std::string format() const override { return midi_out_format; }
    bool accepts_state_while_running() const noexcept override { return true; }
    void set_transport(const TransportInfo& transport) noexcept override;

    [[nodiscard]] std::uint8_t channel() const noexcept {
        return channel_.load(std::memory_order_relaxed);
    }

private:
    std::shared_ptr<MidiOutput> output_;
    std::atomic<std::uint8_t> channel_;
    double sample_rate_ = 48000.0;
    TransportInfo transport_{};
};

} // namespace blokkily
