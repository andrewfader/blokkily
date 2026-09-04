#include "blokkily/instruments/soundfont_synth.hpp"

#include <fluidsynth.h>

#include <algorithm>
#include <cstring>

namespace blokkily {

SoundFontSynth::SoundFontSynth()
    : settings_(new_fluid_settings(), delete_fluid_settings),
      synth_(nullptr, delete_fluid_synth) {
    (void)rebuild(sample_rate_);
}

SoundFontSynth::~SoundFontSynth() = default;

bool SoundFontSynth::rebuild(double sample_rate) {
    if (!settings_) return false;
    fluid_settings_setnum(settings_.get(), "synth.sample-rate", sample_rate);
    fluid_settings_setint(settings_.get(), "synth.threadsafe-api", 0);
    synth_.reset(new_fluid_synth(settings_.get()));
    sample_rate_ = sample_rate;
    return synth_ != nullptr;
}

bool SoundFontSynth::load(const std::filesystem::path& path) {
    if (!synth_ || !std::filesystem::is_regular_file(path)) return false;
    const auto utf8 = path.string();
    const int id = fluid_synth_sfload(synth_.get(), utf8.c_str(), 1);
    if (id == FLUID_FAILED) return false;
    path_ = path;
    return select_preset(bank_, program_);
}

bool SoundFontSynth::select_preset(int bank, int program) {
    if (!synth_ || path_.empty()) return false;
    auto* soundfont = fluid_synth_get_sfont(synth_.get(), 0);
    if (soundfont == nullptr) return false;
    const int soundfont_id = fluid_synth_sfont_select(synth_.get(), 0,
        fluid_sfont_get_id(soundfont));
    if (soundfont_id == FLUID_FAILED) return false;
    if (fluid_synth_bank_select(synth_.get(), 0, bank) == FLUID_FAILED) return false;
    if (fluid_synth_program_change(synth_.get(), 0, program) == FLUID_FAILED) return false;
    bank_ = bank;
    program_ = program;
    return true;
}

bool SoundFontSynth::activate(double sample_rate, std::uint32_t, std::uint32_t) {
    const auto previous_path = path_;
    if (!rebuild(sample_rate)) return false;
    path_.clear();
    return previous_path.empty() || load(previous_path);
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
        if (event.type == PluginEvent::Type::note_on)
            fluid_synth_noteon(synth_.get(), 0, event.key_or_parameter,
                               std::clamp(static_cast<int>(event.value * 127.0), 0, 127));
        else if (event.type == PluginEvent::Type::note_off)
            fluid_synth_noteoff(synth_.get(), 0, event.key_or_parameter);
    }
    render(audio.left.size());
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
