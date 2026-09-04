#pragma once

#include "blokkily/plugins/plugin.hpp"

#include <fluidsynth/types.h>

#include <filesystem>
#include <memory>
#include <string>

namespace blokkily {

class SoundFontSynth final : public PluginInstance {
public:
    SoundFontSynth();
    ~SoundFontSynth() override;
    SoundFontSynth(const SoundFontSynth&) = delete;
    SoundFontSynth& operator=(const SoundFontSynth&) = delete;

    bool load(const std::filesystem::path& path);
    bool select_preset(int bank, int program);
    [[nodiscard]] const std::filesystem::path& loaded_path() const noexcept { return path_; }

    bool activate(double sample_rate, std::uint32_t min_frames,
                  std::uint32_t max_frames) override;
    void process(StereoBlock audio, std::span<const PluginEvent> events) noexcept override;
    std::vector<std::byte> save_state() override;
    bool load_state(std::span<const std::byte> state) override;
    std::string format() const override { return "SoundFont"; }

private:
    using SettingsPtr = std::unique_ptr<fluid_settings_t, void (*)(fluid_settings_t*)>;
    using SynthPtr = std::unique_ptr<fluid_synth_t, void (*)(fluid_synth_t*)>;
    bool rebuild(double sample_rate);

    SettingsPtr settings_;
    SynthPtr synth_;
    std::filesystem::path path_;
    double sample_rate_ = 48000.0;
    int bank_ = 0;
    int program_ = 0;
};

} // namespace blokkily
