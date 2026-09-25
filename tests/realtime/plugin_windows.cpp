// features/plugin_windows.feature: An open editor costs the audio thread
// nothing.
//
// The CLAP fixture with its editor open, embedded in a (recorded) window, its
// timer and display descriptor served by a run loop between blocks the way
// the application's event loop serves them, and its knob turned from the
// editor every hundred blocks. process() and take_parameter_edits(), the
// audio-thread half, may not allocate or free once; the edits taken and the
// level rendered prove the turns really travelled.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/plugin_run_loop.hpp"

#include <dlfcn.h>

#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace {

using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

struct SilentHost final : blokkily::EditorHost {
    void request_resize(std::uint32_t, std::uint32_t) override {}
    void request_show() override {}
    void request_hide() override {}
    void closed() override {}
};

} // namespace

BLOKKILY_REALTIME_CASE(plugin_windows) {
    using namespace blokkily;
    ManualRunLoop loop;
    const ScopedPluginRunLoop installed(loop);
    std::string error;
    auto plugin = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
    require(plugin != nullptr, "the CLAP fixture must instantiate: " + error);
    SilentHost host;
    const NativeParent parent{WindowApi::x11, 0x42, 1.0};
    require(plugin->open_editor(&parent, host, nullptr, &error), "the editor opens: " + error);
    require(loop.timer_count() == 1 && loop.fd_count() == 1, "the editor is served by the run loop");
    require(plugin->activate(48000.0, 1, 256), "the plugin activates");

    void* module = dlopen(BLOKKILY_TEST_CLAP_PATH, RTLD_NOW | RTLD_NOLOAD);
    require(module != nullptr, "the fixture is loaded");
    auto* turn = reinterpret_cast<void (*)(std::uint32_t, double)>(
        dlsym(module, "blokkily_test_gui_turn"));
    dlclose(module);
    require(turn != nullptr, "the fixture exports its knob");

    constexpr std::size_t calls = 1000, block = 256;
    std::vector<float> left(block), right(block);
    std::array<ParameterEdit, 64> edits{};
    std::size_t values = 0;
    float last_peak = 0.0F;
    const PluginEvent note{PluginEvent::Type::note_on, 0, 60, 1.0};
    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        // The main thread's share, outside the armed region: the run loop
        // ticks the editor's timer, and every hundred blocks a knob turns.
        (void)loop.advance(5);
        if (call % 100 == 50) turn(0, 0.3 + 0.01 * static_cast<double>(call / 100));
        const std::span<const PluginEvent> events =
            call == 0 ? std::span<const PluginEvent>{&note, 1} : std::span<const PluginEvent>{};
        std::size_t taken = 0;
        {
            test::AllocationGuard guard;
            plugin->process({left, right}, events);
            taken = plugin->take_parameter_edits(edits);
            total += guard.count();
        }
        for (std::size_t index = 0; index < taken; ++index)
            if (edits[index].kind == ParameterEdit::Kind::value) ++values;
        last_peak = probe::peak(left);
    }
    require_no_allocations(total, "CLAP process() with its editor open");
    require(values == 10, "every turn in the editor reached the host: " + std::to_string(values));
    require(std::abs(last_peak - 0.39F) < 1e-5F, "the last turn is what plays");
    plugin->close_editor();
}
