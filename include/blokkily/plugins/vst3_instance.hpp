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
    // What the plugin says it is: an instrument, or an effect.
    bool instrument = true;
};

// A plugin hosted through JUCE: a VST3 bundle, or (through the same adapter
// and the same JUCE host, features/lv2_hosting.feature) an LV2 bundle. The
// format() it reports says which.
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
    // LV2: the plugins an .lv2 bundle holds (identifier is each one's URI),
    // the folders LV2 plugins are installed in ($LV2_PATH, ~/.lv2,
    // /usr/lib/lv2 ...), and one plugin made by its bundle and URI (the
    // bundle's only plugin when the URI is empty).
    [[nodiscard]] static std::vector<Vst3Descriptor> scan_lv2(const std::filesystem::path& bundle);
    [[nodiscard]] static std::vector<std::filesystem::path> lv2_system_paths();
    [[nodiscard]] static std::unique_ptr<Vst3PluginInstance> create_lv2(
        const std::filesystem::path& bundle, const std::string& uri,
        std::string* error = nullptr);

    bool activate(double sample_rate, std::uint32_t min_frames,
                  std::uint32_t max_frames) override;
    void process(StereoBlock audio, std::span<const PluginEvent> events) noexcept override;
    // JUCE's reset of the hosted plugin (setProcessing and setActive off and
    // on, the VST3 way to flush it), with the retuned-note channels freed.
    void reset() override;
    std::vector<std::byte> save_state() override;
    bool load_state(std::span<const std::byte> state) override;
    std::string format() const override;

    // Plugin boundary v2 (plan F-C). The adapter keeps its automation base in
    // atomics that JUCE parameter listeners update when the plugin moves a
    // parameter itself (its own window, a state load), so modulation is always
    // added to the value the plugin really holds.
    PluginPorts ports() const override;
    std::uint32_t latency_samples() const noexcept override;
    std::uint64_t tail_samples() const noexcept override;
    bool latency_changed() noexcept override;
    std::vector<ParameterInfo> parameters() const override;
    std::optional<double> parameter_value(std::int32_t id) const override;
    // A VST3's class id and its state as a .vstpreset (the SDK's preset
    // file, component and controller state in one), with each listed
    // parameter's ParamID. Nothing for an LV2 plugin.
    std::optional<PortableState> portable_state() override;
    // Loads a .vstpreset portable_state() wrote (or another host did).
    bool load_preset(std::span<const std::byte> preset);
    // Edits the listeners queued from the plugin's own threads. Producers take
    // a mutex (only try_lock on the audio thread); this consumer never locks.
    std::size_t take_parameter_edits(std::span<ParameterEdit> out) noexcept override;
    void set_transport(const TransportInfo& transport) noexcept override;
    void set_sidechain(StereoBlock sidechain) noexcept override;
    void set_aux_output(std::uint32_t output, StereoBlock destination) noexcept override;
    void idle() override;

    // Native editor (item 2.6), through JUCE: embedded in an X11 window or
    // floating as a JUCE top-level. Opening is refused with "Plugin window
    // needs an X11 display" unless DISPLAY names a server the host can open
    // and the parent (if any) is a real window on it, because a JUCE editor
    // without X crashes the process. The host's JUCE queue is pumped from the
    // installed PluginRunLoop while any instance lives. A floating window's
    // close button is served by idle(), which tells the EditorHost.
    bool has_editor() const override;
    bool supports_editor(WindowApi api, bool floating) const override;
    bool open_editor(const NativeParent* parent, EditorHost& host, EditorSize* size,
                     std::string* error) override;
    bool resize_editor(std::uint32_t& width, std::uint32_t& height) override;
    void close_editor() override;
    bool editor_open() const override;
    bool failed() const noexcept override;

private:
    struct Impl;
    explicit Vst3PluginInstance(std::unique_ptr<Impl> implementation);
    std::unique_ptr<Impl> impl_;
};

} // namespace blokkily
