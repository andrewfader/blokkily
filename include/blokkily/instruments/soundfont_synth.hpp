#pragma once

#include "blokkily/plugins/plugin.hpp"

#include <fluidsynth/types.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

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
    // Every voice, reverb and chorus is forgotten and the retuned channels
    // are freed: the renderers are rebuilt from the bank (processing must be
    // stopped).
    void reset() override;
    std::vector<std::byte> save_state() override;
    bool load_state(std::span<const std::byte> state) override;
    std::string format() const override { return "SoundFont"; }

private:
    using SettingsPtr = std::unique_ptr<fluid_settings_t, void (*)(fluid_settings_t*)>;
    using SynthPtr = std::unique_ptr<fluid_synth_t, void (*)(fluid_synth_t*)>;

    static constexpr int shared_channel = 0;
    static constexpr int first_retuned_channel = 1;
    static constexpr int channels = 16;

    // FluidSynth renders in blocks of 64 frames and starts a note only where
    // one of its blocks begins, so a single synth would sound a note up to 63
    // frames late, by an amount that depends on where the stream happened to
    // be rather than on the song: two passes of the same pattern differed.
    // A note is played instead by a renderer whose block begins on the
    // note's own frame: one that is already running in that phase, or an
    // idle one, which starts its blocks there. Every renderer holds the same
    // bank (FluidSynth shares the sample data between them) and all of them
    // are built before processing, so nothing is allocated while it runs.
    static constexpr std::size_t renderer_count = 8;
    static constexpr std::uint64_t block_frames = 64;
    struct Renderer {
        SynthPtr synth{nullptr, nullptr};
        // The key each retuned channel is holding, or -1 when it is free.
        std::array<int, channels> channel_key{};
        // Per-note expression (MPE): a channel whose note is expressive
        // bends over +/-48 semitones (MPE's per-note range) by its
        // expression plus the wheel.
        std::array<bool, channels> channel_expressive{};
        std::array<double, channels> channel_expression{};
        // Frames taken since it started; its blocks begin at multiples of 64.
        std::uint64_t rendered = 0;
        // Frames in a row it has been silent, to know when it can stop.
        std::uint64_t quiet = 0;
        bool running = false;
    };

    bool rebuild(double sample_rate);
    bool load_into(Renderer& renderer, const std::filesystem::path& path);
    bool select_in(Renderer& renderer, int bank, int program);
    void forget_notes() noexcept;
    // Turns every renderer's reverb and chorus on or off. Off only for a bank
    // none of whose voices can send to them, until a controller that could
    // raise a send arrives; then on until the bank is loaded again.
    void set_effects(bool on) noexcept;
    // The renderer that sounds a note starting at the current frame.
    std::size_t choose_renderer() noexcept;
    // Adds `frames` of every running renderer into the output from `offset`.
    void render(StereoBlock audio, std::size_t offset, std::size_t frames) noexcept;
    // A retuned note needs a channel of its own, because pitch bend belongs to
    // a channel and two notes of a microtonal chord are bent differently. Notes
    // in twelve-tone tuning keep the shared channel they always used.
    int claim_channel(Renderer& renderer, int key) noexcept;
    static constexpr int expressive_bend_range = 48;
    [[nodiscard]] int expressive_bend(const Renderer& renderer, int channel) const noexcept;

    // Every renderer is made from the same settings: making a settings
    // object probes FluidSynth's audio drivers, which costs tens of
    // milliseconds each time. Declared before the renderers, so it outlives
    // the synths made from it.
    SettingsPtr settings_{nullptr, nullptr};
    std::vector<Renderer> renderers_;
    // For each key, the renderers holding it on their shared channel (a bit
    // each), so its note-off reaches the voice that plays it.
    std::array<std::uint32_t, 128> shared_holders_{};
    // The wheel as last played.
    int wheel_ = 8192;
    std::vector<float> scratch_left_;
    std::vector<float> scratch_right_;
    std::filesystem::path path_;
    double sample_rate_ = 48000.0;
    // Whether the synth has been through activate() at `sample_rate_`. A second
    // activation at the same rate has nothing to do.
    bool activated_ = false;
    // Built and not yet played: activating at the same rate need not build
    // the renderers again.
    bool fresh_ = false;
    bool effects_on_ = true;
    int bank_ = 0;
    int program_ = 0;
};

} // namespace blokkily
