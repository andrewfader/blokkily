// features/plugin_windows.feature: An open editor costs the audio thread
// nothing.
//
// The CLAP fixture with its editor open, embedded in a (recorded) window, its
// timer and display descriptor served by a run loop between blocks the way
// the application's event loop serves them, and its knob turned from the
// editor every hundred blocks. process() and take_parameter_edits(), the
// audio-thread half, may not allocate or free once; the edits taken and the
// level rendered prove the turns really travelled.
//
// Then the same through the production SongEngine::process(): the CLAP
// synth, its editor open, on track 0 and the VST3 fixture on track 1, each
// knob turned the way its window turns it (for VST3, the gesture JUCE's
// editor makes, sent through the wrapper's component handler: no X display
// is needed for that half of the path). The engine drains every edit into
// its edit ring on the audio thread (drain_edits) without allocating, and
// the control thread takes them off, stamped with the processor's address
// and the song sample of the block they came out of.

#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/plugin_run_loop.hpp"
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

struct SilentHost final : blokkily::EditorHost {
    void request_resize(std::uint32_t, std::uint32_t) override {}
    void request_show() override {}
    void request_hide() override {}
    void closed() override {}
};

template <typename Function>
Function hook(const std::filesystem::path& binary, const char* name) {
    void* module = dlopen(binary.c_str(), RTLD_NOW | RTLD_NOLOAD);
    require(module != nullptr, "the fixture is loaded: " + binary.string());
    auto* function = reinterpret_cast<Function>(dlsym(module, name));
    dlclose(module);
    require(function != nullptr, std::string("the fixture exports ") + name);
    return function;
}

// The CLAP synth with its editor open and the VST3 fixture, both playing a
// held key in a SongEngine, their knobs turned between blocks.
void through_the_engine() {
    using namespace blokkily;
    ManualRunLoop loop;
    const ScopedPluginRunLoop installed(loop);
    std::string error;
    // Declared before the engine, so it outlives the editor it hosts.
    SilentHost host;
    Song song;
    song.patterns = {{"Held", Pattern(1920, 480)}};
    song.tracks = {Track{}, Track{}};
    song.tracks[0].mix.pan = -1.0;
    song.tracks[1].mix.pan = 1.0;
    song.clips = {{0, 0, 0, 1}};
    SongEngine engine;
    engine.set_instrument(0, ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH,
                                                        "dev.blokkily.test", &error));
    engine.set_instrument(1, Vst3PluginInstance::create(BLOKKILY_TEST_VST3_PATH, 0, &error));
    require(engine.has_instrument(0) && engine.has_instrument(1),
            "both fixtures load through the production adapters: " + error);
    const NativeParent parent{WindowApi::x11, 0x43, 1.0};
    auto* clap = engine.processor(track_instrument(0));
    require(clap->open_editor(&parent, host, nullptr, &error), "the CLAP editor opens: " + error);
    require(engine.prepare(song, 48000.0, 256, 0, &error), "prepare: " + error);

    const auto clap_turn =
        hook<void (*)(std::uint32_t, double)>(BLOKKILY_TEST_CLAP_PATH, "blokkily_test_gui_turn");
    const auto vst3_turn = hook<void (*)(float)>(
        std::filesystem::path(BLOKKILY_TEST_VST3_PATH) / "Contents" / "x86_64-linux" /
            "Blokkily Test VST3.so",
        "blokkily_test_vst3_turn");

    constexpr std::size_t calls = 1000, block = 256;
    std::vector<float> left(block), right(block);
    std::size_t clap_values = 0, vst3_values = 0;
    bool stamped = true;
    float clap_level = 0.0F, vst3_level = 0.0F;
    // A key held on both tracks for the whole run.
    (void)engine.play_live(0, {PluginEvent::Type::note_on, 0, 60, 1.0});
    (void)engine.play_live(1, {PluginEvent::Type::note_on, 0, 60, 1.0});
    engine.set_playing(true);
    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        (void)loop.advance(5);
        if (call % 100 == 50) {
            const double turned = 0.3 + 0.01 * static_cast<double>(call / 100);
            clap_turn(0, turned);
            vst3_turn(static_cast<float>(turned));
        }
        const auto position = engine.sample_position();
        {
            test::AllocationGuard guard;
            engine.process({left, right});
            total += guard.count();
        }
        PluginEditEvent edit;
        while (engine.take_plugin_edit(edit)) {
            if (edit.edit.kind != ParameterEdit::Kind::value) continue;
            // The song loops, so the block may begin near its end and the
            // edit be stamped just after its start.
            const auto length = engine.song_samples();
            const auto into = (edit.song_sample + length - position % length) % length;
            stamped = stamped && edit.rolling && into < block;
            if (edit.where == track_instrument(0)) ++clap_values;
            if (edit.where == track_instrument(1)) ++vst3_values;
        }
        clap_level = probe::peak(left);
        vst3_level = probe::peak(right);
    }
    require_no_allocations(total, "SongEngine::process with a plugin editor's edits flowing");
    require(clap_values == 10, "every CLAP editor turn reached the engine's edit ring: " +
                                   std::to_string(clap_values));
    require(vst3_values >= 10, "every VST3 turn reached the engine's edit ring: " +
                                   std::to_string(vst3_values));
    require(stamped, "each edit is stamped inside the block it came out of, rolling");
    require(std::abs(clap_level - 0.39F) < 1e-3F && std::abs(vst3_level - 0.39F) < 6e-3F,
            "the last turns are what both tracks play: " + std::to_string(clap_level) + ", " +
                std::to_string(vst3_level));
    clap->close_editor();
}

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

    through_the_engine();
}
