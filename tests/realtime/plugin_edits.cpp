// features/plugin_boundary.feature: Reporting a plugin's own edits does not
// allocate on the audio thread.
//
// The plugin boundary v2 audio-thread path, through the production adapters:
// set_transport(), process() and take_parameter_edits(), a thousand times per
// format, while the plugin's own window turns a knob every hundred blocks. For
// CLAP the turn travels through request_flush and out_events inside
// process(); for VST3 through JUCE's component handler into the listener
// ring. Not one armed call may allocate or free, and the rendered level and
// the edits taken prove the path really ran.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <dlfcn.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;
constexpr std::size_t turn_every = 100;

template <typename Function>
Function hook(const std::filesystem::path& binary, const char* name) {
    void* library = dlopen(binary.c_str(), RTLD_NOW | RTLD_NOLOAD);
    require(library != nullptr, "the fixture must already be loaded: " + binary.string());
    auto* function = reinterpret_cast<Function>(dlsym(library, name));
    dlclose(library);
    require(function != nullptr, std::string("the fixture must export ") + name);
    return function;
}

// The level the knob is turned to at the n-th turn: distinct every time.
double turned_level(std::size_t turn) { return 0.3 + 0.05 * static_cast<double>(turn % 10); }

template <typename Turn>
void run(blokkily::PluginInstance& plugin, Turn turn, const char* what, bool gesture_per_turn) {
    using namespace blokkily;
    require(plugin.activate(48000.0, 1, block), std::string(what) + " must activate");
    std::vector<float> left(calls * block, -1.0F), right(calls * block, -1.0F);
    std::array<ParameterEdit, 64> edits{};
    std::size_t begins = 0, values = 0, ends = 0;
    const PluginEvent note{PluginEvent::Type::note_on, 0, 60, 1.0};

    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        // The plugin's own window, on the main thread, outside the armed region.
        if (call % turn_every == turn_every / 2) turn(turned_level(call / turn_every));
        const TransportInfo transport{120.0, static_cast<double>(call * block) / 24000.0,
                                      static_cast<std::int32_t>(call * block / 96000), 4, 4,
                                      true};
        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        const std::span<const PluginEvent> events =
            call == 0 ? std::span<const PluginEvent>{&note, 1} : std::span<const PluginEvent>{};
        std::size_t taken = 0;
        {
            test::AllocationGuard guard;
            plugin.set_transport(transport);
            plugin.process({out_left, out_right}, events);
            taken = plugin.take_parameter_edits(edits);
            total += guard.count();
        }
        for (std::size_t index = 0; index < taken; ++index) {
            if (edits[index].kind == ParameterEdit::Kind::begin) ++begins;
            if (edits[index].kind == ParameterEdit::Kind::value) ++values;
            if (edits[index].kind == ParameterEdit::Kind::end) ++ends;
        }
    }
    require_no_allocations(total, what);

    const std::size_t turns = calls / turn_every;
    if (gesture_per_turn)
        require(begins == turns && values == turns && ends == turns,
                std::string(what) + ": every turn must be taken as one gesture");
    else
        require(begins == turns && ends == turns && values >= turns,
                std::string(what) + ": every turn must be taken as a gesture");
    // It really played: before the first turn the fixture holds its default
    // level, and after each turn the level it was turned to.
    const auto level_at = [&](std::size_t call) { return left[call * block + block - 1]; };
    require(std::abs(level_at(turn_every / 2 - 1) - 0.25F) < 0.006F,
            std::string(what) + ": the default level must sound before any turn");
    for (std::size_t turn = 0; turn < turns; ++turn) {
        const std::size_t after = turn * turn_every + turn_every / 2 + 5;
        require(std::abs(level_at(after) - static_cast<float>(turned_level(turn))) < 0.006F,
                std::string(what) + ": turn " + std::to_string(turn) +
                    " must be heard, got " + std::to_string(level_at(after)));
    }
}

} // namespace

BLOKKILY_REALTIME_CASE(plugin_edits) {
    using namespace blokkily;
    std::string error;
    auto clap = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
    require(clap != nullptr, "the CLAP fixture must load through the production adapter: " + error);
    const auto gui_turn = hook<void (*)(std::uint32_t, double)>(BLOKKILY_TEST_CLAP_PATH,
                                                                "blokkily_test_gui_turn");
    run(*clap, [&](double level) { gui_turn(0, level); }, "CLAP", true);

    auto vst3 = Vst3PluginInstance::create(BLOKKILY_TEST_VST3_PATH, 0, &error);
    require(vst3 != nullptr, "the VST3 fixture must load through JUCE: " + error);
    const auto vst3_turn = hook<void (*)(float)>(
        std::filesystem::path(BLOKKILY_TEST_VST3_PATH) / "Contents" / "x86_64-linux" /
            "Blokkily Test VST3.so",
        "blokkily_test_vst3_turn");
    run(*vst3, [&](double level) { vst3_turn(static_cast<float>(level)); }, "VST3", false);
}
