// Plugin boundary v2 (plan F-C, item 1.4), proved through the production
// adapters against the real fixtures: the CLAP fixtures loaded with dlopen
// through their official clap_entry, and the VST3 bundle built by the suite
// and hosted by JUCE. Every audio claim is read from what process() wrote.
//
// Run with a case name; each case is its own CTest test (plugin_<case>).
// features/plugin_boundary.feature maps its scenarios to these cases.

#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <dlfcn.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace blokkily;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

bool near(double actual, double expected, double tolerance = 0.006) {
    return std::abs(actual - expected) < tolerance;
}

// A symbol exported by a fixture binary the adapter already loaded, reached
// without loading a second copy.
template <typename Function>
Function fixture_hook(const std::filesystem::path& binary, const char* name) {
    void* library = dlopen(binary.c_str(), RTLD_NOW | RTLD_NOLOAD);
    require(library != nullptr, "the fixture must already be loaded by the host: " + binary.string());
    auto* hook = reinterpret_cast<Function>(dlsym(library, name));
    dlclose(library);
    require(hook != nullptr, std::string("the fixture must export ") + name);
    return hook;
}

std::string describe(const ParameterEdit& edit) {
    const char* kind = edit.kind == ParameterEdit::Kind::begin   ? "begin"
                       : edit.kind == ParameterEdit::Kind::value ? "value"
                                                                 : "end";
    return std::string(kind) + "(" + std::to_string(edit.parameter) + ", " +
           std::to_string(edit.value) + ")";
}

// ---------------------------------------------------------------- CLAP -----

std::unique_ptr<ClapPluginInstance> clap(bool activate = true,
                                         const char* path = BLOKKILY_TEST_CLAP_PATH,
                                         const char* id = "dev.blokkily.test") {
    std::string error;
    auto plugin = ClapPluginInstance::create(path, id, &error);
    require(plugin != nullptr, "the CLAP fixture must load through the production adapter: " + error);
    if (activate) require(plugin->activate(48000.0, 1, 256), "the CLAP fixture must activate");
    return plugin;
}

// The adapter unloads the library with its last instance, which would leave a
// hook pointing at nothing; a spare, inactive instance keeps it loaded while a
// case holds hooks. It never processes, so it never answers for the others.
std::unique_ptr<ClapPluginInstance> keep_clap_loaded() { return clap(false); }

template <typename Function>
Function clap_hook(const char* name) {
    return fixture_hook<Function>(BLOKKILY_TEST_CLAP_PATH, name);
}

// Renders one 128-frame block and returns its left channel.
std::vector<float> render(PluginInstance& plugin, std::span<const PluginEvent> events = {},
                          float entry = 0.0F) {
    std::vector<float> left(128, entry), right(128, entry);
    plugin.process({left, right}, events);
    return left;
}

// features/plugin_boundary.feature: A plugin lists its parameters.
void clap_parameters() {
    auto plugin = clap(false);
    const auto parameters = plugin->parameters();
    require(parameters.size() == 3, "the fixture declares 3 parameters, got " +
                                        std::to_string(parameters.size()));
    const std::array<const char*, 3> names{"Level", "Tone", "VelocityMode"};
    const std::array<double, 3> defaults{0.25, 0.0, 0.0};
    for (std::size_t index = 0; index < 3; ++index) {
        const auto& parameter = parameters[index];
        require(parameter.id == static_cast<std::int32_t>(index), "ids are 0, 1, 2");
        require(parameter.name == names[index], "parameter " + std::to_string(index) +
                                                     " is " + names[index] + ", got " + parameter.name);
        require(parameter.min == 0.0 && parameter.max == 1.0, "every parameter spans 0..1");
        require(parameter.default_value == defaults[index], "defaults are 0.25, 0, 0");
        require(parameter.automatable, "every parameter is automatable");
    }
}

// features/plugin_boundary.feature: A knob turned in a CLAP plugin's window
// reaches the host as one gesture.
void clap_gui_turn() {
    const auto loaded = keep_clap_loaded();
    const auto turn = clap_hook<void (*)(std::uint32_t, double)>("blokkily_test_gui_turn");
    std::array<ParameterEdit, 16> edits{};
    const auto expect_gesture = [&](std::size_t count, std::uint32_t parameter, double value) {
        require(count == 3, "one gesture is 3 edits, got " + std::to_string(count));
        require(edits[0].kind == ParameterEdit::Kind::begin && edits[0].parameter == 0 &&
                    edits[1].kind == ParameterEdit::Kind::value && edits[1].parameter == 0 &&
                    near(edits[1].value, value) &&
                    edits[2].kind == ParameterEdit::Kind::end && edits[2].parameter == 0,
                "the gesture is begin, value, end on parameter " + std::to_string(parameter) +
                    ", got " + describe(edits[0]) + " " + describe(edits[1]) + " " +
                    describe(edits[2]));
    };
    {
        // Active: the next process() is the flush.
        auto plugin = clap();
        const PluginEvent note{PluginEvent::Type::note_on, 0, 60, 1.0};
        require(render(*plugin, std::span{&note, 1})[127] == 0.25F, "the note starts at 0.25");
        require(plugin->take_parameter_edits(edits) == 0, "nothing was turned yet");
        turn(0, 0.6);
        const auto block = render(*plugin);
        expect_gesture(plugin->take_parameter_edits(edits), 0, 0.6);
        for (const float sample : block)
            require(near(sample, 0.6), "the whole block after the turn renders 0.6, got " +
                                           std::to_string(sample));
        require(plugin->take_parameter_edits(edits) == 0, "a gesture is reported once");
        (void)render(*plugin);
        require(plugin->take_parameter_edits(edits) == 0, "and not again by a later block");
    }
    {
        // Inactive: nothing processes, so idle() flushes on the main thread.
        auto plugin = clap(false);
        turn(0, 0.45);
        require(plugin->take_parameter_edits(edits) == 0, "nothing is flushed before idle()");
        plugin->idle();
        expect_gesture(plugin->take_parameter_edits(edits), 0, 0.45);
        require(plugin->activate(48000.0, 1, 256), "the flushed fixture activates");
        const PluginEvent note{PluginEvent::Type::note_on, 0, 60, 1.0};
        require(near(render(*plugin, std::span{&note, 1})[127], 0.45),
                "the flushed value is what the plugin plays");
    }
    // Edits that do not fit in the caller's span wait for the next call.
    auto plugin = clap();
    turn(0, 0.3);
    (void)render(*plugin);
    std::array<ParameterEdit, 2> small{};
    require(plugin->take_parameter_edits(small) == 2 &&
                plugin->take_parameter_edits(small) == 1 &&
                small[0].kind == ParameterEdit::Kind::end,
            "edits that do not fit stay queued, in order");
}

// features/plugin_boundary.feature: A CLAP plugin can ask which thread it is on.
void clap_thread_check() {
    const auto loaded = keep_clap_loaded();
    const auto report = clap_hook<void (*)(int*)>("blokkily_test_thread_report");
    std::array<int, 6> answers{};
    auto plugin = clap();
    report(answers.data());
    require(answers[0] == 1 && answers[1] == 0,
            "init runs on the main thread, which is not the audio thread");
    std::thread audio([&] { (void)render(*plugin); });
    audio.join();
    report(answers.data());
    require(answers[2] == 0 && answers[3] == 1,
            "process() on the audio thread: not main, audio (got " + std::to_string(answers[2]) +
                ", " + std::to_string(answers[3]) + ")");
    // A host that processes on its main thread is on both at once.
    (void)render(*plugin);
    report(answers.data());
    require(answers[2] == 1 && answers[3] == 1, "process() on the main thread is main and audio");
}

// features/plugin_boundary.feature: A CLAP plugin's main-thread callback runs
// on the main thread.
void clap_request_callback() {
    const auto loaded = keep_clap_loaded();
    const auto request = clap_hook<void (*)()>("blokkily_test_request_callback");
    const auto calls = clap_hook<int (*)()>("blokkily_test_main_thread_calls");
    const auto report = clap_hook<void (*)(int*)>("blokkily_test_thread_report");
    auto plugin = clap();
    const int before = calls();
    std::thread other(request);
    other.join();
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    require(calls() == before, "on_main_thread must not run before idle()");
    plugin->idle();
    require(calls() == before + 1, "idle() runs the requested callback once");
    std::array<int, 6> answers{};
    report(answers.data());
    require(answers[4] == 1 && answers[5] == 0, "the callback runs on the main thread");
    plugin->idle();
    require(calls() == before + 1, "a request is served once");
}

// features/plugin_boundary.feature: An effect hears its input; an instrument
// is given none.
void clap_input_wiring() {
    const auto loaded = keep_clap_loaded();
    {
        const auto inputs_seen = clap_hook<std::uint32_t (*)()>("blokkily_test_inputs_seen");
        auto instrument = clap();
        require(instrument->ports().audio_inputs == 0 && instrument->ports().note_input,
                "the instrument fixture takes notes and no audio");
        const PluginEvent note{PluginEvent::Type::note_on, 0, 60, 1.0};
        // Whatever the block holds on entry, an instrument overwrites it.
        const auto block = render(*instrument, std::span{&note, 1}, 0.9F);
        require(block[0] == 0.25F && block[127] == 0.25F, "the instrument renders its own level");
        require(inputs_seen() == 0, "the instrument fixture was never handed an audio input");
    }
    auto effect = clap(true, BLOKKILY_TEST_CLAP_INPUT_PATH, "dev.blokkily.test.input");
    require(effect->ports().audio_inputs == 2, "the effect fixture takes a stereo input");
    std::vector<float> left(256), right(256);
    for (std::size_t frame = 0; frame < left.size(); ++frame) {
        left[frame] = static_cast<float>(frame) / 256.0F;
        right[frame] = -0.8F;
    }
    effect->process({left, right}, {});
    for (std::size_t frame = 0; frame < left.size(); ++frame)
        require(left[frame] == 0.5F * static_cast<float>(frame) / 256.0F && right[frame] == -0.4F,
                "the effect's output is half its input at frame " + std::to_string(frame));
    const auto aliased = fixture_hook<bool (*)()>(BLOKKILY_TEST_CLAP_INPUT_PATH,
                                                  "blokkily_test_input_aliased");
    require(!aliased(), "the input is a copy, not the output buffer");
    std::vector<float> too_long(512, 1.0F), too_long_right(512, 1.0F);
    effect->process({too_long, too_long_right}, {});
    require(too_long[0] == 0.0F && too_long[511] == 0.0F,
            "a block larger than activate() allowed is silenced, not overrun");
}

// features/plugin_boundary.feature: A project saved before plugin parameters
// existed still opens.
void clap_state() {
    auto legacy = clap();
    const float level = 0.6F;
    std::array<std::byte, sizeof level> old_state{};
    std::memcpy(old_state.data(), &level, sizeof level);
    require(legacy->load_state(old_state), "the old 4-byte state loads");
    const PluginEvent note{PluginEvent::Type::note_on, 0, 60, 1.0};
    require(near(render(*legacy, std::span{&note, 1})[127], 0.6), "the old state's level plays");

    // The current state carries every parameter.
    const PluginEvent dial[]{{PluginEvent::Type::parameter_value, 0, 0, 0.5},
                             {PluginEvent::Type::parameter_value, 0, 1, 1.0},
                             {PluginEvent::Type::parameter_value, 0, 2, 1.0}};
    (void)render(*legacy, dial);
    const auto state = legacy->save_state();
    require(state.size() == 16, "the state is a tag and three parameters");
    auto restored = clap();
    require(restored->load_state(state), "the new state loads");
    // Velocity mode (item 1.6) came back: a key at 0.6 sounds level × 0.6, flat.
    const PluginEvent soft{PluginEvent::Type::note_on, 0, 60, 0.6};
    const auto held = render(*restored, std::span{&soft, 1});
    require(near(held[0], 0.3) && near(held[127], 0.3),
            "level and velocity mode come back: a key at 0.6 sounds 0.3");
    const PluginEvent release[]{{PluginEvent::Type::note_off, 0, 60, 0.0},
                                {PluginEvent::Type::parameter_value, 0, 2, 0.0}};
    (void)render(*restored, release);
    const auto block = render(*restored, std::span{&note, 1});
    float loudest = 0.0F;
    bool varies = false;
    for (const float sample : block) {
        loudest = std::max(loudest, std::abs(sample));
        varies = varies || sample != block[0];
    }
    require(varies && near(loudest, 0.5, 0.02), "tone and level come back: a sine at 0.5");
    const std::array<std::byte, 7> garbage{};
    require(!restored->load_state(garbage), "a stream of the wrong size is refused");
}

// features/plugin_boundary.feature: A plugin reports its latency and tail.
void clap_latency_tail() {
    const auto loaded = keep_clap_loaded();
    const auto set_latency = clap_hook<void (*)(std::uint32_t)>("blokkily_test_set_latency");
    const auto set_tail = clap_hook<void (*)(std::uint32_t)>("blokkily_test_set_tail");
    auto plugin = clap();
    require(plugin->latency_samples() == 0 && plugin->tail_samples() == 0,
            "the fixture starts with no latency and no tail");
    require(!plugin->latency_changed(), "nothing was announced");
    set_latency(64);
    require(plugin->latency_samples() == 0, "a new latency is read only when polled");
    require(plugin->latency_changed(), "the announcement is seen by the poll");
    require(plugin->latency_samples() == 64, "and the new latency is reported");
    require(!plugin->latency_changed(), "an announcement is seen once");
    set_tail(4800);
    require(plugin->tail_samples() == 0, "a new tail is picked up by idle()");
    plugin->idle();
    require(plugin->tail_samples() == 4800, "the tail is reported in samples");
    set_tail(static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max()));
    plugin->idle();
    require(plugin->tail_samples() == std::numeric_limits<std::uint64_t>::max(),
            "INT32_MAX means an endless tail");
    set_latency(32);
    auto fresh = clap();
    require(fresh->latency_samples() == 32, "activation reads the latency");
}

// features/plugin_boundary.feature: A plugin is told where the song is.
void clap_transport() {
    const auto loaded = keep_clap_loaded();
    const auto transport = clap_hook<void (*)(double*)>("blokkily_test_transport");
    auto plugin = clap();
    std::array<double, 3> seen{};
    (void)render(*plugin);
    transport(seen.data());
    require(seen[1] < 0.0, "no transport is sent before the host has one");
    plugin->set_transport({137.5, 10.25, 3, 7, 8, true});
    (void)render(*plugin);
    transport(seen.data());
    require(seen[0] == 137.5 && near(seen[1], 10.25, 1e-6) && seen[2] == 3.0,
            "the plugin sees the tempo, beat position and bar the host set");
}

// ---------------------------------------------------------------- VST3 -----

std::unique_ptr<Vst3PluginInstance> vst3() {
    std::string error;
    auto plugin = Vst3PluginInstance::create(BLOKKILY_TEST_VST3_PATH, 0, &error);
    require(plugin != nullptr, "the VST3 fixture must load through JUCE: " + error);
    require(plugin->activate(48000.0, 1, 256), "the VST3 fixture must activate");
    return plugin;
}

// The level the fixture holds is what it renders while a note sounds (tone 0).
float vst3_level(PluginInstance& plugin, std::span<const PluginEvent> events = {}) {
    return render(plugin, events)[127];
}

using Vst3Turn = void (*)(float);
Vst3Turn vst3_turn_hook() {
    return fixture_hook<Vst3Turn>(std::filesystem::path(BLOKKILY_TEST_VST3_PATH) / "Contents" /
                                      "x86_64-linux" / "Blokkily Test VST3.so",
                                  "blokkily_test_vst3_turn");
}

// Regression (fails before 1.4): a state loaded into a running VST3 moved the
// plugin's parameters but left the adapter's automation base behind, so the
// next modulation was added to the stale value and the plugin jumped back.
void vst3_load_state_base() {
    auto source = vst3();
    const PluginEvent dial{PluginEvent::Type::parameter_value, 0, 0, 0.7};
    const PluginEvent note{PluginEvent::Type::note_on, 0, 60, 1.0};
    const PluginEvent dial_and_note[]{dial, note};
    require(near(vst3_level(*source, dial_and_note), 0.7), "the source renders 0.7");
    const auto state = source->save_state();

    auto target = vst3();
    require(near(vst3_level(*target, std::span{&note, 1}), 0.25), "a fresh instance is 0.25");
    require(target->load_state(state), "the target loads the state while active");
    require(near(vst3_level(*target), 0.7), "the loaded level renders 0.7");
    const PluginEvent modulation{PluginEvent::Type::parameter_modulation, 0, 0, 0.1};
    const float modulated = vst3_level(*target, std::span{&modulation, 1});
    require(near(modulated, 0.8), "a +0.1 modulation after load_state must render 0.8, got " +
                                      std::to_string(modulated));
}

// Regression (fails before 1.4): a knob turned in the plugin's own window
// reached the plugin, but the adapter kept adding modulation to the value it
// had last sent, so the turn was undone by the next modulation.
void vst3_turn_base() {
    auto plugin = vst3();
    const auto turn = vst3_turn_hook();
    const PluginEvent note{PluginEvent::Type::note_on, 0, 60, 1.0};
    require(near(vst3_level(*plugin, std::span{&note, 1}), 0.25), "the note starts at 0.25");
    turn(0.7F);
    // The turn may arrive on the plugin's message thread; bounded poll.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    float level = 0.25F;
    while (std::chrono::steady_clock::now() < deadline) {
        level = vst3_level(*plugin);
        if (near(level, 0.7)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    require(near(level, 0.7), "the turned knob must render 0.7, got " + std::to_string(level));
    const PluginEvent modulation{PluginEvent::Type::parameter_modulation, 0, 0, 0.1};
    const float modulated = vst3_level(*plugin, std::span{&modulation, 1});
    require(near(modulated, 0.8),
            "a +0.1 modulation after the turn must render 0.8, got " + std::to_string(modulated));
}

// features/plugin_boundary.feature: A knob turned in a VST3 plugin's window
// reaches the host as one gesture.
void vst3_turn_edits() {
    auto plugin = vst3();
    const auto turn = vst3_turn_hook();
    const PluginEvent note{PluginEvent::Type::note_on, 0, 60, 1.0};
    (void)render(*plugin, std::span{&note, 1});
    std::array<ParameterEdit, 16> edits{};
    const auto stray = plugin->take_parameter_edits(edits);
    std::string strays;
    for (std::size_t index = 0; index < stray; ++index) strays += describe(edits[index]) + " ";
    require(stray == 0, "nothing is reported before anything moved, got " + strays);
    const PluginEvent automate{PluginEvent::Type::parameter_value, 0, 0, 0.4};
    (void)render(*plugin, std::span{&automate, 1});
    require(plugin->take_parameter_edits(edits) == 0, "not even after an automation event");

    turn(0.7F);
    std::vector<ParameterEdit> received;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        (void)render(*plugin);
        const auto count = plugin->take_parameter_edits(edits);
        received.insert(received.end(), edits.begin(), edits.begin() + static_cast<long>(count));
        if (!received.empty() && received.back().kind == ParameterEdit::Kind::end) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::string seen;
    for (const auto& edit : received) seen += describe(edit) + " ";
    require(received.size() >= 3 && received.front().kind == ParameterEdit::Kind::begin &&
                received.back().kind == ParameterEdit::Kind::end,
            "the turn arrives as one gesture within 2 s, got " + seen);
    bool valued = false;
    for (const auto& edit : received)
        if (edit.kind == ParameterEdit::Kind::value && edit.parameter == 0 && near(edit.value, 0.7))
            valued = true;
    require(valued, "the gesture carries Level = 0.7, got " + seen);
    require(near(vst3_level(*plugin), 0.7), "the note renders at 0.7");
    const PluginEvent modulation{PluginEvent::Type::parameter_modulation, 0, 0, 0.1};
    require(near(vst3_level(*plugin, std::span{&modulation, 1}), 0.8),
            "a +0.1 modulation renders 0.8");
}

// features/plugin_boundary.feature: A plugin lists its parameters.
void vst3_parameters() {
    auto plugin = vst3();
    const auto parameters = plugin->parameters();
    std::string names;
    for (const auto& parameter : parameters) names += parameter.name + ";";
    // JUCE's VST3 wrapper adds a Bypass parameter; its thousands of hidden,
    // non-automatable MIDI CC parameters are not listed.
    require(names == "Level;Tone;Bypass;",
            "the VST3 fixture exposes Level, Tone and the wrapper's Bypass, got " + names);
    require(parameters[0].id == 0 && parameters[0].name == "Level" &&
                near(parameters[0].default_value, 0.25),
            "parameter 0 is Level, default 0.25, got " + parameters[0].name);
    require(parameters[1].id == 1 && parameters[1].name == "Tone" &&
                parameters[1].default_value == 0.0,
            "parameter 1 is Tone, default 0");
    require(plugin->ports().audio_inputs == 0 && plugin->ports().note_input,
            "the VST3 instrument takes notes and no audio");
    require(plugin->latency_samples() == 0 && plugin->tail_samples() == 0 &&
                !plugin->latency_changed(),
            "the VST3 fixture reports no latency and no tail");
    plugin->set_transport({120.0, 4.0, 1, 4, 4, true});
    const PluginEvent note{PluginEvent::Type::note_on, 0, 60, 1.0};
    require(near(vst3_level(*plugin, std::span{&note, 1}), 0.25),
            "a transport-carrying block renders normally");
}

void clap_midi_raw() {
    const auto loaded = keep_clap_loaded();
    const auto last_midi = clap_hook<void (*)(std::uint8_t*)>("blokkily_test_last_midi_bytes");
    auto plugin = clap();
    // Raw MIDI Pitch Bend message: 0xE0, 0x00, 0x40 (center bend)
    const std::uint32_t bend_raw = 0xE0U | (0x00U << 8) | (0x40U << 16);
    const PluginEvent bend_event{PluginEvent::Type::midi_raw, 0, static_cast<std::int32_t>(bend_raw), 0.0, 0.0};
    (void)render(*plugin, std::span{&bend_event, 1});
    std::array<std::uint8_t, 3> bytes{};
    last_midi(bytes.data());
    require(bytes[0] == 0xE0 && bytes[1] == 0x00 && bytes[2] == 0x40,
            "raw pitch bend reaches CLAP plugin through CLAP_EVENT_MIDI");

    // Raw MIDI Mod Wheel CC 1: 0xB0, 0x01, 0x7F
    const std::uint32_t cc_raw = 0xB0U | (0x01U << 8) | (0x7FU << 16);
    const PluginEvent cc_event{PluginEvent::Type::midi_raw, 0, static_cast<std::int32_t>(cc_raw), 0.0, 0.0};
    (void)render(*plugin, std::span{&cc_event, 1});
    last_midi(bytes.data());
    require(bytes[0] == 0xB0 && bytes[1] == 0x01 && bytes[2] == 0x7F,
            "raw CC reaches CLAP plugin through CLAP_EVENT_MIDI");
}

const std::map<std::string, std::function<void()>> cases{
    {"clap_parameters", clap_parameters},
    {"clap_gui_turn", clap_gui_turn},
    {"clap_thread_check", clap_thread_check},
    {"clap_request_callback", clap_request_callback},
    {"clap_input_wiring", clap_input_wiring},
    {"clap_state", clap_state},
    {"clap_latency_tail", clap_latency_tail},
    {"clap_transport", clap_transport},
    {"clap_midi_raw", clap_midi_raw},
    {"vst3_load_state_base", vst3_load_state_base},
    {"vst3_turn_base", vst3_turn_base},
    {"vst3_turn_edits", vst3_turn_edits},
    {"vst3_parameters", vst3_parameters},
};

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "pass a case name");
        const auto found = cases.find(argv[1]);
        require(found != cases.end(), std::string("unknown case ") + argv[1]);
        found->second();
        std::cout << argv[1] << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << (argc > 1 ? argv[1] : "") << ": " << error.what() << '\n';
        return 1;
    }
}
