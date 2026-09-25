#pragma once

#include "blokkily/plugins/plugin.hpp"

#include <fluidsynth/types.h>

#include <array>
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
    // Every voice stops at once and the retuned channels are freed. What
    // FluidSynth's own reverb and chorus hold is not reachable through its
    // API and is left to decay.
    void reset() override;
    std::vector<std::byte> save_state() override;
    bool load_state(std::span<const std::byte> state) override;
    std::string format() const override { return "SoundFont"; }

private:
    using SettingsPtr = std::unique_ptr<fluid_settings_t, void (*)(fluid_settings_t*)>;
    using SynthPtr = std::unique_ptr<fluid_synth_t, void (*)(fluid_synth_t*)>;
    bool rebuild(double sample_rate);
    // A retuned note needs a channel of its own, because pitch bend belongs to
    // a channel and two notes of a microtonal chord are bent differently. Notes
    // in twelve-tone tuning keep the shared channel they always used.
    int claim_channel(int key) noexcept;
    int release_channel(int key) noexcept;

    static constexpr int shared_channel = 0;
    static constexpr int first_retuned_channel = 1;
    static constexpr int channels = 16;
    // The key each retuned channel is holding, or -1 when it is free.
    std::array<int, channels> channel_key_{};

    SettingsPtr settings_;
    SynthPtr synth_;
    std::filesystem::path path_;
    double sample_rate_ = 48000.0;
    // Whether the synth has been through activate() at `sample_rate_`. A second
    // activation at the same rate has nothing to do.
    bool activated_ = false;
    int bank_ = 0;
    int program_ = 0;
};

} // namespace blokkily
