#pragma once

#include "blokkily/plugins/plugin.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace blokkily {

struct Vst3Descriptor {
    std::string name;
    std::string manufacturer;
    std::string identifier;
    std::filesystem::path bundle;
    std::size_t index = 0;
};

class Vst3PluginInstance final : public PluginInstance {
public:
    ~Vst3PluginInstance() override;
    Vst3PluginInstance(Vst3PluginInstance&&) noexcept;
    Vst3PluginInstance& operator=(Vst3PluginInstance&&) noexcept;
    Vst3PluginInstance(const Vst3PluginInstance&) = delete;
    Vst3PluginInstance& operator=(const Vst3PluginInstance&) = delete;

    [[nodiscard]] static std::vector<Vst3Descriptor> scan(
        const std::filesystem::path& bundle);
    [[nodiscard]] static std::vector<Vst3Descriptor> scan_paths(
        const std::vector<std::filesystem::path>& roots);
    [[nodiscard]] static std::vector<std::filesystem::path> system_paths();
    [[nodiscard]] static std::unique_ptr<Vst3PluginInstance> create(
        const std::filesystem::path& bundle, std::size_t index = 0,
        std::string* error = nullptr);

    bool activate(double sample_rate, std::uint32_t min_frames,
                  std::uint32_t max_frames) override;
    void process(StereoBlock audio, std::span<const PluginEvent> events) noexcept override;
    std::vector<std::byte> save_state() override;
    bool load_state(std::span<const std::byte> state) override;
    std::string format() const override { return "VST3"; }

    // Plugin boundary v2 (plan F-C). The adapter keeps its automation base in
    // atomics that JUCE parameter listeners update when the plugin moves a
    // parameter itself (its own window, a state load), so modulation is always
    // added to the value the plugin really holds.
    PluginPorts ports() const override;
    std::uint32_t latency_samples() const noexcept override;
    std::uint64_t tail_samples() const noexcept override;
    bool latency_changed() noexcept override;
    std::vector<ParameterInfo> parameters() const override;
    // Edits the listeners queued from the plugin's own threads. Producers take
    // a mutex (only try_lock on the audio thread); this consumer never locks.
    std::size_t take_parameter_edits(std::span<ParameterEdit> out) noexcept override;
    void set_transport(const TransportInfo& transport) noexcept override;
    void idle() override;

private:
    struct Impl;
    explicit Vst3PluginInstance(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> impl_;
};

} // namespace blokkily
