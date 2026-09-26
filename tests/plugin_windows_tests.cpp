// Native plugin windows (item 2.6), proved through the production CLAP and
// VST3 adapters against the real fixtures, with a ManualRunLoop standing in
// for the application's event loop so every timer tick and every readable
// descriptor is a deterministic step. Each case is its own CTest test.
//
// features/plugin_windows.feature:
//   Scenario: The run loop serves plugin timers and descriptors
//   Scenario: A CLAP editor is embedded in the window the host gives it
//   Scenario: A CLAP editor can float on its own
//   Scenario: What an editor asks of its window reaches the window
//   Scenario: An editor's timer is served while it is open, and only then
//   Scenario: An editor's display connection is watched while it is open
//   Scenario: An instrument destroyed with its editor open closes the editor first
//   Scenario: A knob turned in the editor is one step of history, heard in the song
//   Scenario: An open editor survives a rebuild of the audio graph
//   Scenario: A VST3 editor is refused without an X11 display

#include "editor_gestures.hpp"
#include "engine_graph.hpp"
#include "processor_factory.hpp"
#include "fixtures/test_clap_gui.hpp"
#include "support/audio_probe.hpp"

#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/plugin_run_loop.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace blokkily;
namespace gui = blokkily::test_clap_gui;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 256;
constexpr std::uintptr_t fake_window = 0x5a5a01;
constexpr const char* clap_id = "dev.blokkily.test";

InstrumentSlot clap_slot() { return {"CLAP", BLOKKILY_TEST_CLAP_PATH, clap_id, {}}; }

// The fixture's exported hooks, from the very module the adapter loaded.
void* fixture() {
    static void* module = dlopen(BLOKKILY_TEST_CLAP_PATH, RTLD_NOW);
    require(module != nullptr, "the CLAP fixture must load");
    return module;
}
template <typename Function>
Function hook(const char* name) {
    auto* function = reinterpret_cast<Function>(dlsym(fixture(), name));
    require(function != nullptr, std::string("the CLAP fixture must export ") + name);
    return function;
}
std::array<long, gui::report_size> report() {
    std::array<long, gui::report_size> values{};
    hook<void (*)(long*)>("blokkily_test_gui_report")(values.data());
    return values;
}
std::string gui_log() {
    std::array<char, 4096> text{};
    (void)hook<std::size_t (*)(char*, std::size_t)>("blokkily_test_gui_log")(text.data(), text.size());
    return text.data();
}
void clear_log() { hook<void (*)()>("blokkily_test_gui_clear_log")(); }

// The window side of an editor: records what the plugin asked of it.
struct RecordingHost final : EditorHost {
    std::vector<std::string> calls;
    std::uint32_t width = 0, height = 0;
    int closes = 0;
    void request_resize(std::uint32_t w, std::uint32_t h) override {
        width = w;
        height = h;
        calls.emplace_back("resize");
    }
    void request_show() override { calls.emplace_back("show"); }
    void request_hide() override { calls.emplace_back("hide"); }
    void closed() override {
        ++closes;
        calls.emplace_back("closed");
    }
};

std::unique_ptr<ClapPluginInstance> clap_instance() {
    std::string error;
    auto plugin = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, clap_id, &error);
    require(plugin != nullptr, "the CLAP fixture must instantiate: " + error);
    return plugin;
}

const NativeParent parent{WindowApi::x11, fake_window, 1.5};
const NativeParent wayland_parent{WindowApi::wayland, fake_window, 1.5};

// features/plugin_windows.feature: Scenario: The run loop serves plugin timers and descriptors
void run_loop_manual() {
    ManualRunLoop loop;
    std::vector<std::string> fired;
    const auto fast = loop.add_timer(10, [&] { fired.emplace_back("fast"); });
    std::uint64_t slow = 0;
    slow = loop.add_timer(25, [&] {
        fired.emplace_back("slow");
        // A callback may remove its own timer.
        (void)loop.remove_timer(slow);
    });
    require(fast != 0 && slow != 0 && fast != slow, "timers get distinct ids");
    require(loop.advance(9) == 0, "nothing is due before its period");
    require(loop.advance(16) == 3 && fired == std::vector<std::string>{"fast", "fast", "slow"},
            "timers fire in time order, once per period passed");
    require(loop.timer_count() == 1, "a timer removed from its own callback is gone");
    require(loop.remove_timer(fast) && !loop.remove_timer(fast), "a timer is removed once");

    int reads = 0;
    std::uint32_t seen = 0;
    require(loop.add_fd(7, PluginRunLoop::fd_read, [&](int, std::uint32_t events) {
        ++reads;
        seen = events;
    }), "a descriptor is watched");
    require(!loop.add_fd(7, PluginRunLoop::fd_read, [](int, std::uint32_t) {}),
            "one watch per descriptor");
    require(!loop.signal_fd(7, PluginRunLoop::fd_write), "an event nobody waits for is not delivered");
    require(loop.signal_fd(7, PluginRunLoop::fd_read | PluginRunLoop::fd_write) && reads == 1 &&
                seen == PluginRunLoop::fd_read,
            "the watch hears the events it asked for");
    require(loop.modify_fd(7, PluginRunLoop::fd_write) &&
                loop.watched_events(7) == PluginRunLoop::fd_write,
            "a watch can change what it waits for");
    require(loop.remove_fd(7) && loop.fd_count() == 0 && !loop.signal_fd(7, PluginRunLoop::fd_write),
            "a removed watch hears nothing");

    require(plugin_run_loop() == nullptr, "no run loop is installed by default");
    {
        const ScopedPluginRunLoop installed(loop);
        require(plugin_run_loop() == &loop, "a scoped run loop is installed");
    }
    require(plugin_run_loop() == nullptr, "and removed again");
}

// features/plugin_windows.feature: Scenario: A CLAP editor is embedded in the window the host gives it
void clap_editor_embed() {
    ManualRunLoop loop;
    const ScopedPluginRunLoop installed(loop);
    auto plugin = clap_instance();
    require(plugin->has_editor(), "the fixture has an editor");
    require(plugin->supports_editor(WindowApi::x11, false) &&
                plugin->supports_editor(WindowApi::x11, true) &&
                plugin->supports_editor(WindowApi::wayland, false),
            "the fixture's editor supports embedded X11 and Wayland hosting");
    const auto before = report();
    clear_log();
    RecordingHost host;
    EditorSize size;
    std::string error;
    require(plugin->open_editor(&parent, host, &size, &error), "the editor opens: " + error);
    require(plugin->editor_open(), "the adapter says it is open");
    require(size.width == gui::editor_width && size.height == gui::editor_height && size.resizable,
            "the editor reports its size, in physical pixels");
    const auto opened = report();
    require(opened[gui::creates] == before[gui::creates] + 1 &&
                opened[gui::set_parents] == before[gui::set_parents] + 1 &&
                opened[gui::last_parent] == static_cast<long>(fake_window) &&
                opened[gui::last_floating] == 0 && opened[gui::scale_percent] == 150,
            "the editor was created embedded, scaled, and parented to the host's window");
    require(gui_log() == "create-embedded,scale,size,parent,show",
            "the host follows CLAP's order: create, scale, size, parent, show; got " + gui_log());
    require(!plugin->open_editor(&parent, host, &size, &error) &&
                error == "The editor is already open",
            "a second open is refused");

    std::uint32_t width = 500, height = 30;
    require(plugin->resize_editor(width, height) && width == 500 && height == 60 &&
                report()[gui::width] == 500 && report()[gui::height] == 60,
            "a resize is adjusted by the plugin and applied");

    plugin->close_editor();
    require(!plugin->editor_open() && host.closes == 0,
            "a close the host asked for is not reported back as the editor closing itself");
    require(gui_log() == "create-embedded,scale,size,parent,show,set-size,hide,destroy",
            "closing hides and destroys the editor; got " + gui_log());
    require(report()[gui::open_editors] == before[gui::open_editors], "no editor is left open");
}

// features/plugin_windows.feature: Scenario: A CLAP editor is embedded in a Wayland surface
void clap_editor_wayland_embed() {
    auto plugin = clap_instance();
    RecordingHost host;
    std::string error;
    clear_log();
    require(plugin->open_editor(&wayland_parent, host, nullptr, &error),
            "the Wayland editor opens: " + error);
    const auto opened = report();
    require(opened[gui::last_parent] == static_cast<long>(fake_window) &&
                opened[gui::last_parent_wayland] == 1 && opened[gui::last_floating] == 0,
            "the editor receives the host's native Wayland surface");
    require(gui_log() == "create-embedded,scale,size,parent,show",
            "the Wayland editor is embedded and shown; got " + gui_log());
    plugin->close_editor();
}

// features/plugin_windows.feature: Scenario: A CLAP editor can float on its own
void clap_editor_floating() {
    auto plugin = clap_instance();
    clear_log();
    RecordingHost host;
    EditorSize size;
    std::string error;
    require(plugin->open_editor(nullptr, host, &size, &error), "a floating editor opens: " + error);
    require(report()[gui::last_floating] == 1, "the editor was created floating");
    require(gui_log() == "create-floating,title,show",
            "a floating editor is given a title and shown, never a parent; got " + gui_log());
    std::uint32_t width = 400, height = 300;
    require(!plugin->resize_editor(width, height), "the host does not size a floating window");
    plugin->close_editor();
    require(!plugin->editor_open(), "the floating editor closes");
}

// features/plugin_windows.feature: Scenario: What an editor asks of its window reaches the window
void clap_editor_requests() {
    auto plugin = clap_instance();
    RecordingHost host;
    std::string error;
    require(plugin->open_editor(&parent, host, nullptr, &error), "the editor opens: " + error);
    const auto request_resize =
        hook<void (*)(std::uint32_t, std::uint32_t)>("blokkily_test_gui_request_resize");

    // On the main thread a request reaches the window at once.
    request_resize(480, 260);
    require(host.width == 480 && host.height == 260 && host.calls.back() == "resize",
            "a resize asked on the main thread reaches the window at once");

    // From another thread it waits for idle() on the main thread.
    std::thread([&] { request_resize(640, 360); }).join();
    require(host.width == 480, "a resize asked from another thread does not touch the window there");
    plugin->idle();
    require(host.width == 640 && host.height == 360, "idle() hands it to the window");

    // The plugin closes its own editor; the host acknowledges by destroying
    // it, on the main thread, and the window is told.
    clear_log();
    std::thread([] { hook<void (*)()>("blokkily_test_gui_request_close")(); }).join();
    require(plugin->editor_open() && host.closes == 0, "nothing is torn down off the main thread");
    plugin->idle();
    require(!plugin->editor_open() && host.closes == 1 && host.calls.back() == "closed",
            "idle() destroys the editor and tells the window");
    require(gui_log() == "hide,destroy", "the host acknowledged by destroying it; got " + gui_log());
}

// features/plugin_windows.feature: Scenario: An editor's timer is served while it is open, and only then
void clap_timer_support() {
    const auto before = report();
    {
        // Without a run loop the plugin is told no, and its editor still opens.
        auto plugin = clap_instance();
        RecordingHost host;
        std::string error;
        require(plugin->open_editor(&parent, host, nullptr, &error), "opens without a run loop");
        require(report()[gui::timers_registered] == before[gui::timers_registered],
                "no run loop, no timer");
        plugin->close_editor();
    }
    ManualRunLoop loop;
    const ScopedPluginRunLoop installed(loop);
    auto plugin = clap_instance();
    RecordingHost host;
    std::string error;
    require(plugin->open_editor(&parent, host, nullptr, &error), "the editor opens: " + error);
    require(loop.timer_count() == 1 && report()[gui::timers_registered] == before[gui::timers_registered] + 1,
            "the editor's timer is registered with the run loop");
    const auto ticks = report()[gui::timer_ticks];
    require(loop.advance(100) == 5 && report()[gui::timer_ticks] == ticks + 5,
            "a 20 ms timer ticks five times in 100 ms of the run loop");
    plugin->close_editor();
    require(loop.timer_count() == 0, "closing the editor unregisters its timer");
    (void)loop.advance(100);
    require(report()[gui::timer_ticks] == ticks + 5, "a closed editor's timer never fires");
}

// features/plugin_windows.feature: Scenario: An editor's display connection is watched while it is open
void clap_posix_fd() {
    ManualRunLoop loop;
    const ScopedPluginRunLoop installed(loop);
    auto plugin = clap_instance();
    RecordingHost host;
    std::string error;
    require(plugin->open_editor(&parent, host, nullptr, &error), "the editor opens: " + error);
    require(loop.fd_count() == 1, "the editor's descriptor is watched");
    // The one descriptor the run loop watches is the fixture's pipe.
    int watched = -1;
    for (int fd = 0; fd < 1024 && watched < 0; ++fd)
        if (loop.watched_events(fd) == PluginRunLoop::fd_read) watched = fd;
    require(watched >= 0, "it is watched for reading");
    const auto events = report()[gui::fd_events];
    hook<void (*)()>("blokkily_test_gui_poke")();
    require(loop.signal_fd(watched, PluginRunLoop::fd_read) &&
                report()[gui::fd_events] == events + 1,
            "a readable descriptor reaches the plugin's on_fd, which reads what arrived");
    plugin->close_editor();
    require(loop.fd_count() == 0, "closing the editor stops the watch");
}

// features/plugin_windows.feature: Scenario: An instrument destroyed with its editor open closes the editor first
void clap_destroy_with_editor() {
    ManualRunLoop loop;
    const ScopedPluginRunLoop installed(loop);
    auto plugin = clap_instance();
    RecordingHost host;
    std::string error;
    require(plugin->open_editor(&parent, host, nullptr, &error), "the editor opens: " + error);
    clear_log();
    plugin.reset();
    require(host.closes == 1, "the window is told its editor is gone");
    require(gui_log() == "hide,destroy,plugin-destroy",
            "the editor is destroyed before the plugin; got " + gui_log());
    require(loop.timer_count() == 0 && loop.fd_count() == 0,
            "nothing the plugin registered is left in the run loop");
}

// --- Through the engine ------------------------------------------------------------

Song song_of(std::size_t tracks) {
    Song song;
    song.patterns = {{"Windows", Pattern(4096, 24000)}};
    song.tracks.assign(tracks, Track{});
    for (std::size_t track = 0; track < tracks; ++track) {
        song.tracks[track].name = "T" + std::to_string(track);
        song.tracks[track].instrument = clap_slot();
    }
    song.clips = {{0, 0, 0, 1}};
    return song;
}

struct Built {
    std::unique_ptr<SongEngine> engine;
    std::unique_ptr<RtAudioOutput> output;
    GraphBuild build;
};
Built build(const Song& song, std::vector<ReleasedProcessor> adopted) {
    Built built;
    built.engine = std::make_unique<SongEngine>();
    built.build = populate_graph(*built.engine, song, std::move(adopted), {});
    std::string error;
    require(built.engine->prepare(song, rate, block, 0, &error), "prepare: " + error);
    load_fresh_state(*built.engine, song, built.build);
    built.output = std::make_unique<RtAudioOutput>(RtAudioOutput::Mode::deterministic);
    require(built.output->open(*built.engine) && built.output->start(), "the device starts");
    return built;
}
std::vector<float> pump(RtAudioOutput& output) {
    std::vector<float> stereo(2 * 1024, -1.0F);
    require(output.pump(stereo), "the production callback renders");
    stereo.resize(1024);   // the left channel
    return stereo;
}
// The fixture's level on track 0, heard on the bus: one live key.
float level_heard(Built& built, const Song& song) {
    require(built.engine->play_live(0, {PluginEvent::Type::note_on, 0, 60, 1.0, 0.0}), "live note");
    const float peak = probe::peak(pump(*built.output));
    require(built.engine->play_live(0, {PluginEvent::Type::note_off, 0, 60, 0.0, 0.0}), "release");
    (void)pump(*built.output);
    return peak / strip_gain(song.tracks[0].mix, false).left;
}

// features/plugin_windows.feature: Scenario: A knob turned in the editor is one step of history, heard in the song
void editor_gesture_step() {
    auto song = song_of(1);
    auto built = build(song, {});
    auto* instrument = built.engine->processor(track_instrument(0));
    require(instrument != nullptr, "the CLAP fixture plays track 0");
    RecordingHost host;
    std::string error;
    require(instrument->open_editor(&parent, host, nullptr, &error), "the editor opens: " + error);

    const auto turn = hook<void (*)(std::uint32_t, double)>("blokkily_test_gui_turn");
    EditGestures gestures;
    std::vector<EditGestures::Completed> steps;
    std::vector<ParameterEdit::Kind> kinds;
    const auto drain = [&] {
        PluginEditEvent event;
        while (built.engine->take_plugin_edit(event)) {
            require(event.where == track_instrument(0), "the edit names the instrument's address");
            kinds.push_back(event.edit.kind);
            if (auto step = gestures.feed(event)) steps.push_back(*step);
        }
    };
    require(std::abs(level_heard(built, song) - 0.25F) < 1e-4F, "the fixture starts at 0.25");
    drain();
    require(steps.empty(), "playing a note is not an edit");

    turn(0, 0.6);
    (void)pump(*built.output);   // the audio thread flushes the turn
    drain();
    require(kinds == std::vector<ParameterEdit::Kind>{ParameterEdit::Kind::begin,
                                                      ParameterEdit::Kind::value,
                                                      ParameterEdit::Kind::end},
            "the turn arrives through the engine's ring as one gesture");
    require(steps.size() == 1 && steps[0].where == track_instrument(0) && steps[0].parameter == 0 &&
                std::abs(steps[0].value - 0.6) < 1e-9,
            "one gesture is one step of history");
    require(std::abs(level_heard(built, song) - 0.6F) < 1e-4F, "the turned level is heard");

    turn(0, 0.4);
    (void)pump(*built.output);
    drain();
    require(steps.size() == 2 && std::abs(steps[1].value - 0.4) < 1e-9,
            "a second turn is a second step");
    require(gestures.last_value() && std::abs(gestures.last_value()->value - 0.4) < 1e-9,
            "the readout follows the last value");

    // A value with no gesture around it is not a knob turn.
    PluginEditEvent loose{track_instrument(0), 0, false, {ParameterEdit::Kind::value, 0, 0.9, 0}};
    require(!gestures.feed(loose), "a value outside any gesture makes no step");
    // Nested gestures on one processor are one step, taken when the last closes.
    const auto at = [](ParameterEdit::Kind kind, std::int32_t parameter, double value) {
        return PluginEditEvent{track_instrument(0), 0, false, {kind, parameter, value, 0}};
    };
    require(!gestures.feed(at(ParameterEdit::Kind::begin, 0, 0)) &&
                !gestures.feed(at(ParameterEdit::Kind::begin, 1, 0)) &&
                !gestures.feed(at(ParameterEdit::Kind::value, 1, 0.7)) &&
                !gestures.feed(at(ParameterEdit::Kind::end, 1, 0)) &&
                gestures.open(track_instrument(0)),
            "a gesture still open holds the step back");
    require(gestures.feed(at(ParameterEdit::Kind::end, 0, 0)).has_value(),
            "the step is taken when the last gesture closes");
    require(!gestures.feed(at(ParameterEdit::Kind::begin, 0, 0)) &&
                !gestures.feed(at(ParameterEdit::Kind::end, 0, 0)),
            "a click that moved nothing makes no step");
    instrument->close_editor();
}

// features/plugin_windows.feature: Scenario: An open editor survives a rebuild of the audio graph
void editor_survives_adoption() {
    ManualRunLoop loop;
    const ScopedPluginRunLoop installed(loop);
    auto song = song_of(1);
    auto first = build(song, {});
    auto* instrument = first.engine->processor(track_instrument(0));
    RecordingHost host;
    std::string error;
    require(instrument->open_editor(&parent, host, nullptr, &error), "the editor opens: " + error);
    const auto opened = report();

    // A track is added: the graph changes, track 0's instrument does not.
    auto grown = song;
    grown.tracks.push_back(grown.tracks[0]);
    first.output->stop();
    auto adopted = adopt_processors(first.engine->release_processors(), graph_signature(song),
                                    graph_signature(grown), nullptr);
    first.output.reset();
    first.engine.reset();
    auto second = build(grown, std::move(adopted));
    require(second.engine->processor(track_instrument(0)) == instrument,
            "the rebuild adopts the instance with the open editor");
    require(instrument->editor_open() && host.closes == 0 && report()[gui::destroys] == opened[gui::destroys],
            "its editor stayed open through the rebuild, never destroyed");
    require(loop.timer_count() == 1, "and its timer still runs");
    const auto ticks = report()[gui::timer_ticks];
    (void)loop.advance(40);
    require(report()[gui::timer_ticks] == ticks + 2, "served by the same run loop");

    // Track 0 swaps instrument: its instance goes, and its editor with it.
    auto swapped = grown;
    swapped.tracks[0].instrument = InstrumentSlot{};
    second.output->stop();
    clear_log();
    auto kept = adopt_processors(second.engine->release_processors(), graph_signature(grown),
                                 graph_signature(swapped), nullptr);
    require(kept.size() == 1 && kept.front().where == track_instrument(1), "only track 1 is kept");
    require(host.closes == 1, "the swapped instrument's window is told its editor is gone");
    require(gui_log() == "hide,destroy,plugin-destroy",
            "its editor is destroyed before it; got " + gui_log());
    require(loop.timer_count() == 0, "and its timer is gone from the run loop");
}

// features/plugin_windows.feature: Scenario: A VST3 editor is refused without an X11 display
void vst3_editor_refused() {
    // CTest points DISPLAY at a socket that does not exist.
    const char* display = std::getenv("DISPLAY");
    require(display != nullptr && std::string(display).rfind("/nonexistent", 0) == 0,
            "the test must run with DISPLAY at the nonexistent socket CTest sets");
    std::string error;
    auto plugin = Vst3PluginInstance::create(BLOKKILY_TEST_VST3_PATH, 0, &error);
    require(plugin != nullptr, "the VST3 fixture must instantiate: " + error);
    require(plugin->has_editor() && plugin->supports_editor(WindowApi::x11, false) &&
                plugin->supports_editor(WindowApi::x11, true),
            "the fixture has an X11 editor");
    RecordingHost host;
    for (const NativeParent* where : {&parent, static_cast<const NativeParent*>(nullptr)}) {
        error.clear();
        require(!plugin->open_editor(where, host, nullptr, &error),
                "an editor with no display must be refused, not attempted");
        require(error == "Plugin window needs an X11 display", "the refusal says why: " + error);
        require(!plugin->editor_open() && host.closes == 0, "nothing was opened");
    }
    // The instrument is unharmed by the refusal.
    require(plugin->activate(rate, 1, block), "the instrument still activates");
    std::vector<float> left(block), right(block);
    const PluginEvent note{PluginEvent::Type::note_on, 0, 60, 1.0};
    plugin->process({left, right}, std::span{&note, 1});
    require(std::abs(probe::peak(left) - 0.25F) < 1e-4F, "and plays");
}

} // namespace

int main(int argc, char** argv) {
    const std::map<std::string, void (*)()> cases{
        {"run_loop", run_loop_manual},
        {"clap_embed", clap_editor_embed},
        {"clap_wayland_embed", clap_editor_wayland_embed},
        {"clap_floating", clap_editor_floating},
        {"clap_requests", clap_editor_requests},
        {"clap_timer", clap_timer_support},
        {"clap_posix_fd", clap_posix_fd},
        {"clap_destroy_open", clap_destroy_with_editor},
        {"gesture_step", editor_gesture_step},
        {"survives_adoption", editor_survives_adoption},
        {"vst3_refused", vst3_editor_refused},
    };
    if (argc != 2 || !cases.contains(argv[1])) {
        std::cerr << "usage: blokkily_plugin_windows_tests <case>\n";
        return 2;
    }
    try {
        cases.at(argv[1])();
    } catch (const std::exception& failure) {
        std::cerr << argv[1] << ": " << failure.what() << '\n';
        return 1;
    }
    std::cout << argv[1] << ": ok\n";
    return 0;
}
