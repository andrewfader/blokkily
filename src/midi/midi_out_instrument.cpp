#include "blokkily/midi/midi_out_instrument.hpp"

#include <algorithm>
#include <cmath>

namespace blokkily {

MidiOutInstrument::MidiOutInstrument(std::shared_ptr<MidiOutput> output, std::uint8_t channel)
    : output_(std::move(output)), channel_(static_cast<std::uint8_t>(channel & 0x0FU)) {}

MidiOutInstrument::~MidiOutInstrument() {
    // Nothing the song struck is left ringing on the synthesizer.
    if (output_ != nullptr)
        output_->send_now({0, transport_.frame,
                           {static_cast<std::uint8_t>(0xB0U | channel()), 123, 0}, 3});
}

bool MidiOutInstrument::activate(double sample_rate, std::uint32_t, std::uint32_t) {
    sample_rate_ = sample_rate > 0.0 ? sample_rate : 48000.0;
    return true;
}

void MidiOutInstrument::set_transport(const TransportInfo& transport) noexcept {
    transport_ = transport;
}

void MidiOutInstrument::process(StereoBlock audio, std::span<const PluginEvent> events) noexcept {
    std::fill(audio.left.begin(), audio.left.end(), 0.0F);
    std::fill(audio.right.begin(), audio.right.end(), 0.0F);
    if (output_ == nullptr || transport_.heard_ns == 0) return;
    const auto channel = this->channel();
    for (const auto& event : events) {
        TimedMidi message;
        message.frame = transport_.frame + event.sample_offset;
        message.due_ns = transport_.heard_ns +
                         static_cast<std::uint64_t>(std::llround(
                             static_cast<double>(event.sample_offset) * 1e9 / sample_rate_));
        switch (event.type) {
        case PluginEvent::Type::note_on: {
            if (event.key_or_parameter < 0 || event.key_or_parameter > 127) continue;
            const auto velocity = static_cast<std::uint8_t>(
                std::clamp<long>(std::lround(event.value * 127.0), 1, 127));
            message.bytes = {static_cast<std::uint8_t>(0x90U | channel),
                             static_cast<std::uint8_t>(event.key_or_parameter), velocity};
            message.size = 3;
            break;
        }
        case PluginEvent::Type::note_off:
            if (event.key_or_parameter < 0 || event.key_or_parameter > 127) continue;
            message.bytes = {static_cast<std::uint8_t>(0x80U | channel),
                             static_cast<std::uint8_t>(event.key_or_parameter), 0};
            message.size = 3;
            break;
        case PluginEvent::Type::midi_raw: {
            // A controller movement, said on this instrument's channel.
            const auto raw = static_cast<std::uint32_t>(event.key_or_parameter);
            const auto status = static_cast<std::uint8_t>((raw & 0xF0U) | channel);
            const auto kind = raw & 0xF0U;
            if (kind < 0xA0U || kind > 0xE0U || kind == 0xC0U) continue;
            message.bytes = {status, static_cast<std::uint8_t>((raw >> 8) & 0x7FU),
                             static_cast<std::uint8_t>((raw >> 16) & 0x7FU)};
            message.size = kind == 0xD0U ? 2 : 3;
            break;
        }
        default: continue;
        }
        (void)output_->send(message);
    }
}

std::vector<std::byte> MidiOutInstrument::save_state() {
    return {static_cast<std::byte>(channel())};
}

bool MidiOutInstrument::load_state(std::span<const std::byte> state) {
    if (state.empty()) return true;
    const auto channel = static_cast<std::uint8_t>(state.front());
    if (channel > 15) return false;
    channel_.store(channel, std::memory_order_relaxed);
    return true;
}

} // namespace blokkily
