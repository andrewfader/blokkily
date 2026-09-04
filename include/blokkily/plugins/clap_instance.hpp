#pragma once

#include "blokkily/plugins/plugin.hpp"

#include <filesystem>
#include <memory>
#include <string>

namespace blokkily {

class ClapPluginInstance final : public PluginInstance {
public:
    ~ClapPluginInstance() override;
    ClapPluginInstance(ClapPluginInstance&&) noexcept;
    ClapPluginInstance& operator=(ClapPluginInstance&&) noexcept;
    ClapPluginInstance(const ClapPluginInstance&) = delete;
    ClapPluginInstance& operator=(const ClapPluginInstance&) = delete;

    [[nodiscard]] static std::unique_ptr<ClapPluginInstance> create(
        const std::filesystem::path& library, const std::string& plugin_id,
        std::string* error = nullptr);

    bool activate(double sample_rate, std::uint32_t min_frames,
                  std::uint32_t max_frames) override;
    void process(StereoBlock audio, std::span<const PluginEvent> events) noexcept override;
    std::vector<std::byte> save_state() override;
    bool load_state(std::span<const std::byte> state) override;
    std::string format() const override { return "CLAP"; }
    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;

private:
    struct Impl;
    explicit ClapPluginInstance(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> impl_;
};

} // namespace blokkily
