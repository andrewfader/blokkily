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
    // clap_plugin.reset(), on the calling thread marked as the audio thread
    // (the one that processes next), while the plugin is active.
    void reset() override;
    std::vector<std::byte> save_state() override;
    bool load_state(std::span<const std::byte> state) override;
    std::string format() const override { return "CLAP"; }

    // Plugin boundary v2 (plan F-C). The host answers the plugin through a
    // real host_extension table: params (rescan, clear, request_flush),
    // thread-check, latency, tail and audio-ports.
    PluginPorts ports() const override;
    std::uint32_t latency_samples() const noexcept override;
    std::uint64_t tail_samples() const noexcept override;
    bool latency_changed() noexcept override;
    std::vector<ParameterInfo> parameters() const override;
    // Edits the plugin pushed through out_events during process(), or during
    // a main-thread flush while inactive, in the order it pushed them.
    std::size_t take_parameter_edits(std::span<ParameterEdit> out) noexcept override;
    void set_transport(const TransportInfo& transport) noexcept override;
    // Runs a requested on_main_thread callback, serves a requested flush while
    // the plugin is inactive, and picks up an announced tail change.
    void idle() override;

    // Native editor (item 2.6): clap.gui, embedded in the window the host
    // gives or floating on its own. The adapter answers the plugin's
    // clap.timer-support and clap.posix-fd-support requests through the
    // installed PluginRunLoop, and destroys an open editor before the plugin,
    // telling its EditorHost. What the plugin asks of its window from another
    // thread (resize, show, hide, closed) reaches the EditorHost from idle(),
    // on the main thread.
    bool has_editor() const override;
    bool supports_editor(WindowApi api, bool floating) const override;
    bool open_editor(const NativeParent* parent, EditorHost& host, EditorSize* size,
                     std::string* error) override;
    bool resize_editor(std::uint32_t& width, std::uint32_t& height) override;
    void set_editor_scale(double scale) override;
    void close_editor() override;
    bool editor_open() const override;
    [[nodiscard]] const std::string& id() const noexcept;
    [[nodiscard]] const std::string& name() const noexcept;

private:
    struct Impl;
    explicit ClapPluginInstance(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> impl_;
};

} // namespace blokkily
