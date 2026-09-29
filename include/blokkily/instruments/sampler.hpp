#pragma once

// The built-in sampler (design 2, plan item 1.9): plays a SamplerProgram as an
// instrument behind the ordinary PluginInstance boundary.
//
// Threads. Everything except process() and take_parameter_edits() runs on the
// control thread. A program is decoded (through the shared AudioAssetCache, at
// each file's own rate) into an immutable kit on the control thread and handed
// to the render callback by one atomic pointer exchange, so load_state() is
// safe while audio runs (accepts_state_while_running() is true). Voices that
// were sounding keep playing the kit they started from until they end. Old
// kits are freed on the control thread once the callback reports, by epoch,
// that no voice can still read them: the audio thread never frees, allocates,
// locks, or touches a shared_ptr count.
//
// Sound. A note picks every zone whose key and velocity range holds it. A
// pitch-tracking zone plays at exp2((key + cents/100 - root - fine/100 +
// tune) / 12) times file rate / engine rate; one that does not track pitch
// plays the file at its own pitch. Samples are read with 4-point Hermite
// interpolation, loops wrap forward or ping-pong, and each voice follows a
// linear-segment ADSR. A voice's level is velocity (linear, 0..1) × zone gain
// × the gain parameter, and its zone's pan is a balance.

#include "blokkily/audio/audio_asset.hpp"
#include "blokkily/instruments/sampler_program.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace blokkily {

// The sampler's parameters, as PluginEvent::key_or_parameter carries them.
// Every value is normalised to 0..1 and acts on top of the program, so a
// pattern lock never rewrites a zone. A parameter_value sets the base; a
// parameter_modulation sets an offset added to it (the two stay distinct);
// the sum is clamped to 0..1.
namespace sampler_parameter {
inline constexpr std::int32_t gain = 0;      // -60..+12 dB, linear in dB; 0 = silent
inline constexpr std::int32_t tune = 1;      // -24..+24 semitones; 0.5 = none
inline constexpr std::int32_t attack = 2;    // attack time × 4^(2v - 1); 0.5 = ×1
inline constexpr std::int32_t decay = 3;     // decay time × 4^(2v - 1)
inline constexpr std::int32_t sustain = 4;   // sustain level × 2v, at most 1
inline constexpr std::int32_t release = 5;   // release time × 4^(2v - 1)
inline constexpr std::int32_t start = 6;     // start offset, a fraction of the region
inline constexpr std::int32_t count = 7;
} // namespace sampler_parameter

// The level a velocity (0..1) plays at: linear.
[[nodiscard]] constexpr double sampler_velocity_gain(double velocity) noexcept {
    return velocity < 0.0 ? 0.0 : velocity > 1.0 ? 1.0 : velocity;
}

// How quickly a voice falls silent when it is stolen or choked.
inline constexpr std::uint32_t sampler_fast_release_frames = 64;

class SamplerInstrument final : public PluginInstance {
public:
    // `cache` is shared with the rest of the application and must outlive
    // this instrument; null gives the sampler a private cache.
    explicit SamplerInstrument(AudioAssetCache* cache = nullptr);
    ~SamplerInstrument() override;
    SamplerInstrument(const SamplerInstrument&) = delete;
    SamplerInstrument& operator=(const SamplerInstrument&) = delete;

    // The directory relative sample paths are resolved against, and that
    // save_state() writes paths under relative to (plan F-F). Takes effect at
    // the next set_program()/load_state().
    void set_base_directory(std::filesystem::path directory);
    [[nodiscard]] const std::filesystem::path& base_directory() const noexcept {
        return base_directory_;
    }

    // Validates and decodes the program and hands it to the render callback.
    // False, changing nothing, for an invalid program. A zone whose file is
    // missing or unreadable does not fail the program: it stays in it (so it
    // round-trips) and is silent, and missing_samples() names it.
    bool set_program(const SamplerProgram& program, std::string* error = nullptr);
    // The last accepted program, with sample paths as they were given.
    [[nodiscard]] const SamplerProgram& program() const noexcept { return program_; }
    // One entry per zone of program() that could not be loaded:
    // "zone <index>: <path>: <reason>".
    [[nodiscard]] const std::vector<std::string>& missing_samples() const noexcept {
        return missing_;
    }

    // The decoded file a zone plays (at its own rate), or null with a reason.
    [[nodiscard]] AudioAssetPtr asset_for(const SamplerZone& zone, std::string* error = nullptr);
    // A zone for `file` that starts from what the file says about itself: its
    // root key and its sustain loop (a WAV `smpl` or AIFF `INST` chunk) when it
    // has them. The path is stored relative to the base directory when it lies
    // inside it. Nullopt, with a reason, when the file cannot be decoded.
    [[nodiscard]] std::optional<SamplerZone> make_zone(const std::filesystem::path& file,
                                                       std::string* error = nullptr);

    // Frees every kit the render callback can no longer reach. Runs at the
    // start of every set_program() and save_state(); control thread.
    void collect();
    // Kits currently held (the live one, a queued one, and any a sounding voice
    // still reads). For tests of the handoff.
    [[nodiscard]] std::size_t kit_count() const noexcept { return kits_.size(); }

    bool activate(double sample_rate, std::uint32_t min_frames,
                  std::uint32_t max_frames) override;
    void process(StereoBlock audio, std::span<const PluginEvent> events) noexcept override;
    // Every voice, fading ones included, falls silent at once.
    void reset() override;
    std::vector<std::byte> save_state() override;
    bool load_state(std::span<const std::byte> state) override;
    std::string format() const override { return "Sampler"; }
    bool accepts_state_while_running() const noexcept override { return true; }
    std::uint64_t tail_samples() const noexcept override;
    std::vector<ParameterInfo> parameters() const override;

    struct Kit;
    struct Zone;

private:
    struct Voice {
        const Zone* zone = nullptr;
        std::uint64_t epoch = 0;   // of the kit `zone` belongs to
        std::uint64_t age = 0;
        double position = 0.0;
        double step = 0.0;         // frames per output sample, before the tune parameter
        double level = 0.0;
        double attack_step = 0.0, decay_step = 0.0, release_step = 0.0;
        double sustain_level = 1.0;
        double release_frames = 0.0;
        float left_gain = 0.0F, right_gain = 0.0F;
        int key = -1;
        // Per-note pitch expression (MPE), in semitones, on top of the tune
        // parameter and the wheel.
        double expression_semitones = 0.0;
        enum class Stage : std::uint8_t { attack, decay, sustain, release } stage = Stage::attack;
        bool active = false;
        bool backward = false;     // ping-pong direction
        bool in_loop = false;      // has turned at a loop boundary at least once
        bool may_loop = false;     // started before the loop's end
        bool pedal_held = false;   // released while the sustain pedal was down
    };
    // Per-note expression (MPE): the pitch of every voice of that key; the
    // sampler has no timbre and no pressure, so those are ignored.
    void apply_expression(const PluginEvent& event) noexcept;
    // Raw MIDI (wave 4.1): the pitch wheel (two semitones either way) and the
    // sustain pedal (CC 64); the rest is ignored.
    void apply_midi(std::uint32_t raw) noexcept;

    void start_note(const PluginEvent& event) noexcept;
    void release_key(int key) noexcept;
    void render(StereoBlock audio, std::size_t from, std::size_t to) noexcept;
    bool render_voice(Voice& voice, StereoBlock audio, std::size_t from, std::size_t to,
                      double gain, double tune) noexcept;
    [[nodiscard]] double parameter(std::int32_t id) const noexcept;
    static void fast_release(Voice& voice) noexcept;

    std::unique_ptr<AudioAssetCache> own_cache_;
    AudioAssetCache* cache_;
    std::filesystem::path base_directory_;
    SamplerProgram program_;
    std::vector<std::string> missing_;

    // Control thread: every kit not yet proved unreachable.
    std::vector<std::unique_ptr<Kit>> kits_;
    std::uint64_t next_epoch_ = 1;
    // Handoff: the control thread stores a new kit here; the callback takes it.
    std::atomic<Kit*> queued_{nullptr};
    // Published by the callback after each block: no voice or live kit has an
    // epoch below this. 0 until the callback has run.
    std::atomic<std::uint64_t> oldest_live_epoch_{0};

    // Audio thread only.
    const Kit* live_ = nullptr;
    std::array<Voice, sampler_max_voices> voices_{};
    // Stolen voices finishing their fast release, so the slot is free at once.
    std::array<Voice, 16> fading_{};
    std::uint64_t voice_clock_ = 0;
    int program_polyphony_ = 32;
    std::array<double, sampler_parameter::count> base_{};
    std::array<double, sampler_parameter::count> modulation_{};
    double engine_rate_ = 48000.0;
    std::atomic<std::uint64_t> tail_frames_{0};
    // Audio thread only: where the wheel is, in semitones, and the pedal.
    double bend_semitones_ = 0.0;
    bool pedal_down_ = false;
};

} // namespace blokkily
