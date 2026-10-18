#pragma once

// Plugin isolation (features/plugin_isolation.feature). A third-party plugin
// runs in a helper process of its own (blokkily_plugin_host), behind the same
// PluginInstance the engine already speaks to, so a plugin that crashes or
// hangs costs that helper and the track goes silent; the window, the song and
// every other plugin carry on.
//
// Audio crosses in shared memory, one block behind: the audio thread hands
// block N to the helper and plays what the helper made of the blocks before
// it, so it never waits on another process (it neither locks nor blocks).
// The block of delay is reported as the processor's latency, which the
// engine's delay compensation lines up with every other track. An offline
// render (a bounce, a freeze, an analysis) and a deterministic test pump
// wait for the helper instead, so they hear exactly what a punctual helper
// would have made; set_sandbox_waits() says which.
//
// The main-thread calls (state, parameters, editors) are requests over a
// pipe, answered within a deadline. Linux only for now; elsewhere
// sandbox_supported() is false and plugins load in the application.

#include "blokkily/model/song.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

namespace blokkily {

[[nodiscard]] bool sandbox_supported() noexcept;

// The processor `slot` names, running in a fresh helper started from `host`,
// with its state not yet loaded; nullptr with the reason in `error` when the
// helper could not be started or the plugin would not load in it.
[[nodiscard]] std::unique_ptr<PluginInstance> create_sandboxed(const InstrumentSlot& slot,
                                                               const std::filesystem::path& host,
                                                               std::string* error);

// Whether sandboxed processors wait for their helper's block (true: offline
// renders and deterministic pumps) or play silence for a block it is late
// with (false: a real device). Any thread; read on the audio thread.
void set_sandbox_waits(bool waits) noexcept;
[[nodiscard]] bool sandbox_waits() noexcept;

// The most frames the audio device asks for in one callback. A sandboxed
// processor activated after this is one such period behind (at least its
// largest block), so a whole callback's worth of blocks has a period to come
// back in. Capped at 4096.
void set_sandbox_period(std::uint32_t frames) noexcept;
[[nodiscard]] std::uint32_t sandbox_period() noexcept;

// Sets set_sandbox_waits(true) for a scope and restores what was there.
class ScopedSandboxWait {
public:
    ScopedSandboxWait() noexcept;
    ~ScopedSandboxWait();
    ScopedSandboxWait(const ScopedSandboxWait&) = delete;
    ScopedSandboxWait& operator=(const ScopedSandboxWait&) = delete;

private:
    bool previous_;
};

// The plugin formats a helper loads (CLAP, VST3, LV2), created in this
// process: what the helper runs, and what the application runs when
// isolation is off. nullptr for any other format.
[[nodiscard]] std::unique_ptr<PluginInstance> create_format_processor(const InstrumentSlot& slot,
                                                                      std::string* error);
[[nodiscard]] bool is_plugin_format(const std::string& format);

// The helper's main(): serves one processor over the descriptors and shared
// memory its arguments name, until the application closes the pipe.
int run_sandbox_host(int argc, char** argv);

} // namespace blokkily
