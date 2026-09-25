#pragma once

// What every built-in effect shares: the PluginInstance surface, parameter
// bookkeeping, sample-accurate event handling, and the state stream. A
// concrete effect only says how to prepare its buffers, how to follow new
// parameter values, and how to render a run of samples.
//
// Threads. activate(), load_state(), save_state() and parameters() are
// control-thread calls made while the effect is not processing. process()
// and set_transport() are audio-thread calls and never allocate, lock, log or
// do I/O. tail_samples() may be called from any thread, so the parameter
// values it reads are atomics.

#include "blokkily/effects/builtin.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <span>
#include <string>

namespace blokkily::effects {

struct ParameterSpec {
    const char* name;
    double min, max, default_value;
};

class BuiltinEffect : public PluginInstance {
public:
    ~BuiltinEffect() override = default;
    BuiltinEffect(const BuiltinEffect&) = delete;
    BuiltinEffect& operator=(const BuiltinEffect&) = delete;

    bool activate(double sample_rate, std::uint32_t min_frames,
                  std::uint32_t max_frames) override;
    void process(StereoBlock audio, std::span<const PluginEvent> events) noexcept override;
    std::vector<std::byte> save_state() override;
    bool load_state(std::span<const std::byte> state) override;
    std::string format() const override { return std::string(builtin_effect_format); }
    PluginPorts ports() const override { return {2, false}; }
    std::vector<ParameterInfo> parameters() const override;
    void set_transport(const TransportInfo& transport) noexcept override;
    // Clears every bit of signal state (clear()), keeping the parameters.
    void reset() override;

    [[nodiscard]] const std::string& identifier() const noexcept { return identifier_; }

protected:
    BuiltinEffect(std::string identifier, std::span<const ParameterSpec> specs);

    // The value a parameter has now: its automation base plus modulation,
    // clamped to its range. Audio thread (inside prepare/update/render).
    [[nodiscard]] double value(std::size_t id) const noexcept;
    // The automation base alone, from any thread.
    [[nodiscard]] double base(std::size_t id) const noexcept;
    [[nodiscard]] double sample_rate() const noexcept { return sample_rate_; }
    // The song tempo of the last set_transport(), 120 until one arrives.
    [[nodiscard]] double bpm() const noexcept { return bpm_.load(std::memory_order_relaxed); }

    // Control thread, from activate(): size buffers for `sample_rate` and
    // clear every bit of signal state.
    virtual void prepare(double sample_rate) = 0;
    // While not processing: forget the signal held (lines, filter memories,
    // envelopes) without resizing anything. Allocates nothing.
    virtual void clear() noexcept = 0;
    // Audio thread: a parameter or the tempo changed; recompute coefficients.
    virtual void update() noexcept = 0;
    // Audio thread: process `frames` samples in place.
    virtual void render(float* left, float* right, std::size_t frames) noexcept = 0;

private:
    void apply(const PluginEvent& event) noexcept;

    std::string identifier_;
    std::span<const ParameterSpec> specs_;
    std::unique_ptr<std::atomic<double>[]> base_;
    std::unique_ptr<double[]> modulation_;
    std::atomic<double> bpm_{120.0};
    double sample_rate_ = 48000.0;
    bool active_ = false;
    // Parameters or tempo moved since update() last ran.
    std::atomic<bool> dirty_{true};
};

std::unique_ptr<PluginInstance> make_eq3();
std::unique_ptr<PluginInstance> make_delay();
std::unique_ptr<PluginInstance> make_reverb();
std::unique_ptr<PluginInstance> make_compressor();

} // namespace blokkily::effects
