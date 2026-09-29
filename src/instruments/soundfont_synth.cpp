#include "blokkily/instruments/soundfont_synth.hpp"

#include <fluidsynth.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace blokkily {

SoundFontSynth::SoundFontSynth()
    : settings_(new_fluid_settings(), delete_fluid_settings),
      synth_(nullptr, delete_fluid_synth) {
    channel_key_.fill(-1);
    (void)rebuild(sample_rate_);
}

SoundFontSynth::~SoundFontSynth() = default;

bool SoundFontSynth::rebuild(double sample_rate) {
    if (!settings_) return false;
    fluid_settings_setnum(settings_.get(), "synth.sample-rate", sample_rate);
    fluid_settings_setint(settings_.get(), "synth.threadsafe-api", 0);
    synth_.reset(new_fluid_synth(settings_.get()));
    sample_rate_ = sample_rate;
    activated_ = false;
    return synth_ != nullptr;
}

bool SoundFontSynth::load(const std::filesystem::path& path) {
    if (!synth_ || !std::filesystem::is_regular_file(path)) return false;
    // Asking for the bank that is already open only changes the preset. A
    // General MIDI bank is hundreds of megabytes, and reloading it every time
    // the session is saved, restored, or recompiled would stall the interface
    // for a change that touches nothing the bank holds.
    if (path_ == path && fluid_synth_get_sfont(synth_.get(), 0) != nullptr)
        return select_preset(bank_, program_);
    const auto utf8 = path.string();
    const int id = fluid_synth_sfload(synth_.get(), utf8.c_str(), 1);
    if (id == FLUID_FAILED) return false;
    path_ = path;
    // The bank a project asked for may not be in the file it was pointed at —
    // a percussion bank is missing from most single-instrument SoundFonts. The
    // file is still loaded, so it falls back to the first General MIDI program
    // rather than being reported as a failure and going silent.
    if (select_preset(bank_, program_)) return true;
    return select_preset(0, 0);
}

bool SoundFontSynth::select_preset(int bank, int program) {
    if (!synth_ || path_.empty()) return false;
    auto* soundfont = fluid_synth_get_sfont(synth_.get(), 0);
    if (soundfont == nullptr) return false;
    const int soundfont_id = fluid_synth_sfont_select(synth_.get(), 0,
        fluid_sfont_get_id(soundfont));
    if (soundfont_id == FLUID_FAILED) return false;
    // Every channel carries the same preset: a microtonal chord is one
    // instrument spread over several channels, not several instruments.
    for (int channel = 0; channel < channels; ++channel) {
        if (fluid_synth_bank_select(synth_.get(), channel, bank) == FLUID_FAILED) return false;
        if (fluid_synth_program_change(synth_.get(), channel, program) == FLUID_FAILED)
            return false;

    }
    // A bank and program the file does not actually hold leaves the channel
    // with nothing to sound, and FluidSynth reports that as success. Asking the
    // synth what it ended up with is the only way to know the preset is real,
    // and a silent instrument is exactly what a producer cannot debug.
    if (fluid_synth_get_channel_preset(synth_.get(), shared_channel) == nullptr) return false;
    bank_ = bank;
    program_ = program;
    return true;
}

bool SoundFontSynth::activate(double sample_rate, std::uint32_t, std::uint32_t) {
    // Re-activating at the rate the synth already runs at leaves it exactly as
    // it was, so recompiling an arrangement does not rebuild the synth and
    // re-read its bank underneath a note that is still sounding.
    if (activated_ && synth_ && sample_rate == sample_rate_) return true;
    const auto previous_path = path_;
    if (!rebuild(sample_rate)) return false;
    path_.clear();
    if (!previous_path.empty() && !load(previous_path)) return false;
    // Retuning a SoundFont is a tuning table's job rather than the bend wheel's:
    // a wheel is scaled by whatever sensitivity the instrument was written with,
    // while a tuning says the pitch of a key in cents and means it. Each voice
    // channel gets a table of its own so that two notes of a microtonal chord
    // can be tuned differently at the same time.
    std::array<double, 128> twelve_tone{};
    for (std::size_t key = 0; key < twelve_tone.size(); ++key)
        twelve_tone[key] = static_cast<double>(key) * 100.0;
    for (int channel = first_retuned_channel; channel < channels; ++channel) {
        if (fluid_synth_activate_key_tuning(synth_.get(), 0, channel, "blokkily",
                                            twelve_tone.data(), 1) == FLUID_FAILED)
            return false;
        if (fluid_synth_activate_tuning(synth_.get(), channel, 0, channel, 1) == FLUID_FAILED)
            return false;
    }
    activated_ = true;
    return true;
}

int SoundFontSynth::claim_channel(int key) noexcept {
    for (int channel = first_retuned_channel; channel < channels; ++channel)
        if (channel_key_[static_cast<std::size_t>(channel)] < 0) {
            channel_key_[static_cast<std::size_t>(channel)] = key;
            return channel;
        }
    // Every channel is busy, so the oldest retuned voice gives way rather than
    // the new note being dropped.
    channel_key_[first_retuned_channel] = key;
    return first_retuned_channel;
}

int SoundFontSynth::release_channel(int key) noexcept {
    for (int channel = first_retuned_channel; channel < channels; ++channel)
        if (channel_key_[static_cast<std::size_t>(channel)] == key) {
            channel_key_[static_cast<std::size_t>(channel)] = -1;
            return channel;
        }
    return -1;
}

void SoundFontSynth::process(StereoBlock audio,
                             std::span<const PluginEvent> events) noexcept {
    if (!synth_ || audio.left.size() != audio.right.size()) return;
    std::size_t cursor = 0;
    const auto render = [&](std::size_t end) {
        const auto frames = static_cast<int>(end - cursor);
        if (frames > 0)
            fluid_synth_write_float(synth_.get(), frames, audio.left.data() + cursor, 0, 1,
                                    audio.right.data() + cursor, 0, 1);
        cursor = end;
    };
    for (const auto& event : events) {
        const auto offset = std::min<std::size_t>(event.sample_offset, audio.left.size());
        render(offset);
        if (event.type == PluginEvent::Type::note_on) {
            const int velocity = std::clamp(static_cast<int>(event.value * 127.0), 0, 127);
            if (event.cents == 0.0) {
                fluid_synth_noteon(synth_.get(), shared_channel, event.key_or_parameter,
                                   velocity);
            } else {
                const int channel = claim_channel(event.key_or_parameter);
                // The channel's tuning table is told what this key is worth in
                // cents, and the note is then played as any other note.
                const int key = event.key_or_parameter;
                const double tuned = key * 100.0 + event.cents;
                fluid_synth_tune_notes(synth_.get(), 0, channel, 1, const_cast<int*>(&key),
                                       const_cast<double*>(&tuned), 1);
                fluid_synth_noteon(synth_.get(), channel, key, velocity);
            }
        } else if (event.type == PluginEvent::Type::note_off) {
            const int channel = release_channel(event.key_or_parameter);
            fluid_synth_noteoff(synth_.get(), channel < 0 ? shared_channel : channel,
                                event.key_or_parameter);
        } else if (event.type == PluginEvent::Type::midi_raw) {
            // The track is one instrument, so the channel a controller was
            // played on names nothing here. A channel-wide message reaches
            // every channel the synth sounds notes on: the shared one and
            // each retuned voice's, so a keyboard on channel 2 bends the
            // notes it plays and a microtonal chord bends with the wheel.
            const auto raw = static_cast<std::uint32_t>(event.key_or_parameter);
            const auto kind = static_cast<std::uint8_t>(raw & 0xF0U);
            const auto first = static_cast<int>((raw >> 8) & 0x7FU);
            const auto second = static_cast<int>((raw >> 16) & 0x7FU);
            if (kind == 0xA0) {
                // Poly pressure belongs to the channel holding that key.
                int channel = shared_channel;
                for (int candidate = first_retuned_channel; candidate < channels; ++candidate)
                    if (channel_key_[static_cast<std::size_t>(candidate)] == first) channel = candidate;
                fluid_synth_key_pressure(synth_.get(), channel, first, second);
            } else {
                for (int channel = 0; channel < channels; ++channel) {
                    if (kind == 0xE0) fluid_synth_pitch_bend(synth_.get(), channel, first | (second << 7));
                    else if (kind == 0xB0) fluid_synth_cc(synth_.get(), channel, first, second);
                    else if (kind == 0xD0) fluid_synth_channel_pressure(synth_.get(), channel, first);
                }
            }
        }
    }
    render(audio.left.size());
}

void SoundFontSynth::reset() {
    // This hook is called only with processing stopped (offline export).
    // all_sounds_off/system_reset leave FluidSynth's buffered output and
    // oscillator/effect phase behind. Recreate the renderer at its current
    // rate and reload the same preset, including its per-voice tuning tables,
    // so a fresh pass cannot contain earlier audio or depend on block phase.
    // File loading and allocation stay here, never in process().
    activated_ = false;
    (void)activate(sample_rate_, 1, 1);
    channel_key_.fill(-1);
}

std::vector<std::byte> SoundFontSynth::save_state() {
    const std::string value = path_.string() + '\n' + std::to_string(bank_) + '\n' +
                              std::to_string(program_);
    std::vector<std::byte> bytes(value.size());
    std::memcpy(bytes.data(), value.data(), value.size());
    return bytes;
}

bool SoundFontSynth::load_state(std::span<const std::byte> state) {
    const std::string value(reinterpret_cast<const char*>(state.data()), state.size());
    const auto first = value.find('\n');
    const auto second = first == std::string::npos ? first : value.find('\n', first + 1);
    if (first == std::string::npos || second == std::string::npos) return false;
    try {
        const int bank = std::stoi(value.substr(first + 1, second - first - 1));
        const int program = std::stoi(value.substr(second + 1));
        return load(value.substr(0, first)) && select_preset(bank, program);
    } catch (...) {
        return false;
    }
}

} // namespace blokkily
