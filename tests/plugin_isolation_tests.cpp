// features/plugin_isolation.feature: a plugin running in a helper process of
// its own, through the real helper and the real CLAP and VST3 fixtures. What
// it plays is compared sample for sample with the same plugin in this
// process; a plugin that crashes or hangs costs its helper and plays
// silence, and this process carries on.

#include "processor_factory.hpp"
#include "support/audio_probe.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/plugins/sandbox.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace blokkily;
using namespace std::chrono_literals;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 512;

InstrumentSlot clap_slot() { return {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", {}}; }
InstrumentSlot faulty_slot() { return {"CLAP", BLOKKILY_FAULTY_CLAP_PATH, "dev.blokkily.faulty", {}}; }
InstrumentSlot effect_slot() {
    return {"CLAP", BLOKKILY_TEST_CLAP_EFFECT_PATH, "dev.blokkily.test.effect", {}};
}
InstrumentSlot vst3_slot() { return {"VST3", BLOKKILY_TEST_VST3_PATH, "", {}}; }

std::unique_ptr<PluginInstance> isolated(const InstrumentSlot& slot) {
    std::string error;
    auto instance = create_sandboxed(slot, BLOKKILY_PLUGIN_HOST_PATH, &error);
    require(instance != nullptr, "the plugin loads in its own process: " + error);
    return instance;
}

std::unique_ptr<PluginInstance> in_process(const InstrumentSlot& slot) {
    std::string error;
    auto instance = create_format_processor(slot, &error);
    require(instance != nullptr, "the plugin loads in this process: " + error);
    return instance;
}

// `blocks` blocks of the plugin, a note at `onset` in the first held until
// the last, rendered one after another into one signal (left channel).
std::vector<float> play(PluginInstance& plugin, std::size_t blocks, std::uint32_t onset, int key = 60) {
    std::vector<float> heard;
    std::vector<float> left(block), right(block);
    for (std::size_t index = 0; index < blocks; ++index) {
        std::fill(left.begin(), left.end(), 0.0F);
        std::fill(right.begin(), right.end(), 0.0F);
        std::vector<PluginEvent> events;
        if (index == 0)
            events.push_back({PluginEvent::Type::note_on, onset, key, 0.8});
        if (index + 1 == blocks)
            events.push_back({PluginEvent::Type::note_off, 0, key, 0.0});
        TransportInfo transport{120.0, 0.0, 0, 4, 4, true};
        plugin.set_transport(transport);
        plugin.process({left, right}, events);
        heard.insert(heard.end(), left.begin(), left.end());
    }
    return heard;
}

std::size_t first_sound(const std::vector<float>& signal) {
    for (std::size_t index = 0; index < signal.size(); ++index)
        if (std::abs(signal[index]) > 1e-6F) return index;
    return signal.size();
}

// Scenario: An isolated plugin plays what it plays in the window, one block later
void matches(const InstrumentSlot& slot, const char* name) {
    const ScopedSandboxWait waits;
    auto here = in_process(slot);
    auto there = isolated(slot);
    require(there->format() == here->format(), "it is still the same format");
    require(here->activate(rate, 1, block) && there->activate(rate, 1, block), "both activate");
    require(there->latency_samples() == here->latency_samples() + block,
            "it reports one block more latency, for the engine to compensate");
    const std::uint32_t onset = 100;
    const auto near = play(*here, 8, onset);
    const auto far = play(*there, 8, onset);
    const auto starts = first_sound(far);
    std::cout << name << ": in process from " << first_sound(near) << ", isolated from " << starts << '\n';
    require(first_sound(near) < near.size(), "the note sounds");
    require(starts == first_sound(near) + block, "the note at its sample offset, one block later");
    float worst = 0.0F;
    for (std::size_t index = 0; index + block < far.size(); ++index)
        worst = std::max(worst, std::abs(far[index + block] - near[index]));
    std::cout << name << ": largest difference " << worst << '\n';
    require(worst < 1e-6F, "sample for sample the same sound");
}

// Scenario: Behind a device that asks for long periods, a whole period behind
void period() {
    const ScopedSandboxWait waits;
    set_sandbox_period(2048);
    auto here = in_process(clap_slot());
    auto there = isolated(clap_slot());
    require(here->activate(rate, 1, block) && there->activate(rate, 1, block), "both activate");
    set_sandbox_period(0);
    require(there->latency_samples() == here->latency_samples() + 2048,
            "a 2048-frame device period: the helper is a period behind");
    // A 2048-frame callback is handed over in the engine's 128-frame chunks,
    // all of them in flight before the first comes back.
    const auto run = [](PluginInstance& plugin) {
        std::vector<float> heard;
        std::vector<float> left(128), right(128);
        for (int piece = 0; piece < 96; ++piece) {
            std::fill(left.begin(), left.end(), 0.0F);
            std::fill(right.begin(), right.end(), 0.0F);
            std::vector<PluginEvent> events;
            if (piece == 1) events.push_back({PluginEvent::Type::note_on, 7, 60, 0.8});
            plugin.process({left, right}, events);
            heard.insert(heard.end(), left.begin(), left.end());
        }
        return heard;
    };
    const auto near = run(*here);
    const auto far = run(*there);
    require(first_sound(far) == first_sound(near) + 2048, "the note, a period later");
    float worst = 0.0F;
    for (std::size_t index = 0; index + 2048 < far.size(); ++index)
        worst = std::max(worst, std::abs(far[index + 2048] - near[index]));
    require(worst < 1e-6F, "sample for sample");
}

// Scenario: Its parameters and state cross with it
void state() {
    auto there = isolated(clap_slot());
    auto here = in_process(clap_slot());
    require(there->activate(rate, 1, block), "activates");
    const auto listed = there->parameters();
    require(!listed.empty() && listed.size() == here->parameters().size(),
            "it lists the plugin's parameters");
    std::vector<float> left(block), right(block);
    const PluginEvent change{PluginEvent::Type::parameter_value, 0, listed.front().id, 0.25};
    {
        const ScopedSandboxWait waits;
        there->process({left, right}, std::span{&change, 1});
        there->process({left, right}, {});
    }
    const auto value = there->parameter_value(listed.front().id);
    require(value && std::abs(*value - 0.25) < 1e-9, "a parameter moved in the helper reads back");
    const auto saved = there->save_state();
    require(!saved.empty(), "its state is saved through the helper");
    require(here->load_state(saved), "and loads into the same plugin anywhere");
    const auto loaded = here->parameter_value(listed.front().id);
    require(loaded && std::abs(*loaded - 0.25) < 1e-9, "carrying the parameter");
    auto again = isolated(clap_slot());
    require(again->load_state(saved) && again->parameter_value(listed.front().id) == value,
            "state loads into a helper as well");
}

// Waits (at most `limit`) for the helper to be noticed gone.
bool noticed_failed(PluginInstance& plugin, std::chrono::milliseconds limit) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (std::chrono::steady_clock::now() < deadline) {
        plugin.idle();
        if (plugin.failed()) return true;
        std::this_thread::sleep_for(20ms);
    }
    return false;
}

// Scenario: A plugin that crashes costs only its helper
void crash() {
    auto there = isolated(faulty_slot());
    require(there->activate(rate, 1, block), "activates");
    const auto saved = there->save_state();
    {
        const ScopedSandboxWait waits;
        require(probe::peak(play(*there, 4, 0, 60)) > 0.1F, "it plays until it is broken");
    }
    // Key 127 crashes it, as a real plugin's bug would.
    (void)play(*there, 2, 0, 127);
    require(noticed_failed(*there, 5s), "the crashed helper is noticed");
    const auto after = play(*there, 4, 0, 60);
    require(probe::peak(after) == 0.0F, "and the plugin plays silence, without calling it");
    require(there->save_state() == saved, "its last known state is still there to reload");
    std::cout << "crash: this process is still running\n";
}

// Scenario: A plugin that hangs is stopped
void hang() {
    auto there = isolated(faulty_slot());
    require(there->activate(rate, 1, block), "activates");
    // Real time: the audio thread never waits for the helper, so a block it
    // is late with plays silence and the callback returns on time.
    const auto started = std::chrono::steady_clock::now();
    const auto heard = play(*there, 40, 0, 126);
    require(std::chrono::steady_clock::now() - started < 1s, "the audio thread is never held up");
    require(probe::peak(heard) == 0.0F, "a hung plugin is heard as silence");
    require(noticed_failed(*there, 8s), "after a few seconds of nothing it is called hung and stopped");
}

// Scenario: A plugin that will not load is refused
void refused() {
    std::string error;
    auto missing = create_sandboxed({"CLAP", "/nonexistent/plugin.clap", "x", {}},
                                    BLOKKILY_PLUGIN_HOST_PATH, &error);
    std::cout << "refused: " << error << '\n';
    require(missing == nullptr && !error.empty(), "a missing plugin is refused, saying why");
    error.clear();
    auto no_helper = create_sandboxed(clap_slot(), "/nonexistent/blokkily_plugin_host", &error);
    require(no_helper == nullptr && !error.empty(), "so is a helper that is not there");
}

// Scenario: An isolated effect hears its parameters and their modulation in time
void effect() {
    const ScopedSandboxWait waits;
    auto here = in_process(effect_slot());
    auto there = isolated(effect_slot());
    require(here->activate(rate, 1, block) && there->activate(rate, 1, block), "both activate");
    require(there->latency_samples() == here->latency_samples() + block, "one block later");
    // A steady input; Gain set, then modulated, part way through blocks, in
    // pieces of 128 as the engine's modulation chunks are.
    const auto run = [](PluginInstance& plugin) {
        std::vector<float> heard;
        std::vector<float> left(128), right(128);
        for (int piece = 0; piece < 48; ++piece) {
            std::fill(left.begin(), left.end(), 0.5F);
            std::fill(right.begin(), right.end(), 0.5F);
            std::vector<PluginEvent> events;
            if (piece == 8) events.push_back({PluginEvent::Type::parameter_value, 40, 0, 0.75});
            if (piece == 20) events.push_back({PluginEvent::Type::parameter_modulation, 0, 0, -0.5});
            if (piece == 30) events.push_back({PluginEvent::Type::parameter_modulation, 64, 0, 0.0});
            plugin.process({left, right}, events);
            heard.insert(heard.end(), left.begin(), left.end());
        }
        return heard;
    };
    const auto near = run(*here);
    const auto far = run(*there);
    float worst = 0.0F;
    for (std::size_t index = 0; index + block < far.size(); ++index)
        worst = std::max(worst, std::abs(far[index + block] - near[index]));
    std::cout << "effect: levels " << near[8 * 128 + 200] << " -> " << near[20 * 128 + 200]
              << ", largest difference " << worst << '\n';
    require(std::abs(near[8 * 128 + 200] - near[20 * 128 + 200]) > 0.05F, "modulation is heard");
    require(worst < 1e-6F, "the isolated effect hears every value and modulation where it lands");
}

// Scenario: A macro turned while the song plays reaches an isolated instrument
void macro() {
    const ScopedSandboxWait waits;
    Song song;
    constexpr Tick ticks_per_beat = 24000;
    song.patterns = {{"Held", Pattern(96000, ticks_per_beat)}};
    Trigger note;
    note.start = 0;
    note.duration = 95000;
    note.musical_data = Note{60, 1.0F, 0.0F};
    (void)song.patterns[0].pattern.add(note);
    song.tracks.assign(1, Track{});
    song.tracks[0].name = "held";
    song.tracks[0].instrument = clap_slot();
    song.clips = {{0, 0, 0, 1}};
    Modulator turned;
    turned.kind = Modulator::Kind::macro;
    turned.name = "Macro";
    turned.value = 0.0;
    turned.targets = {{track_instrument(0), 0, 0.4}};
    song.modulators = {turned};
    const auto run = [&](std::unique_ptr<PluginInstance> instrument) {
        Song playing = song;
        SongEngine engine;
        engine.set_instrument(0, std::move(instrument));
        std::string error;
        require(engine.prepare(playing, rate, block, 0, &error), "prepare: " + error);
        engine.set_playing(true);
        std::vector<float> heard;
        std::vector<float> left(1024), right(1024);
        for (int pump = 0; pump < 12; ++pump) {
            if (pump == 6) {
                playing.modulators[0].value = 1.0;
                engine.apply_mix(playing);
            }
            engine.process({left, right});
            heard.insert(heard.end(), left.begin(), left.end());
        }
        return heard;
    };
    const auto near = run(in_process(clap_slot()));
    const auto far = run(isolated(clap_slot()));
    const auto peak_over = [](const std::vector<float>& signal, std::size_t from, std::size_t to) {
        return probe::peak(std::span<const float>(signal).subspan(from, to - from));
    };
    std::cout << "macro: in process " << peak_over(near, 4096, 6144) << " -> "
              << peak_over(near, 6144, 7168) << "; isolated " << peak_over(far, 4096 + block, 6144 + block)
              << " -> " << peak_over(far, 6144 + block, 7168 + block) << '\n';
    float worst = 0.0F;
    for (std::size_t index = 0; index + block < far.size(); ++index)
        worst = std::max(worst, std::abs(far[index + block] - near[index]));
    std::cout << "macro: largest difference " << worst << '\n';
    require(peak_over(near, 6144, 7168) > peak_over(near, 4096, 6144) * 1.5F, "the macro is heard");
    require(worst < 1e-5F, "the isolated instrument hears it on the same sample, one block later");
}

// What an editor's window is told.
struct RecordingHost final : EditorHost {
    int resizes = 0, shows = 0, hides = 0, closes = 0;
    void request_resize(std::uint32_t, std::uint32_t) override { ++resizes; }
    void request_show() override { ++shows; }
    void request_hide() override { ++hides; }
    void closed() override { ++closes; }
};

// Scenario: An isolated plugin's editor opens, and is closed with it
// (regression: a rebuild left the window holding a plugin that had gone, and
// the application crashed closing it)
void editor() {
    RecordingHost host;
    {
        auto there = isolated(clap_slot());
        require(there->has_editor(), "the plugin says it has an editor");
        require(there->supports_editor(native_window_api(), true), "a floating one");
        EditorSize size;
        std::string error;
        require(there->open_editor(nullptr, host, &size, &error), "it opens: " + error);
        require(there->editor_open(), "and is open");
        there->idle();
    }
    require(host.closes == 1, "the window is told when the plugin goes, as it is in process");
    RecordingHost crashed;
    auto crashing = isolated(faulty_slot());
    EditorSize size;
    std::string error;
    require(crashing->open_editor(nullptr, crashed, &size, &error), "it opens: " + error);
    require(crashing->activate(rate, 1, block), "activates");
    (void)play(*crashing, 2, 0, 127);
    require(noticed_failed(*crashing, 5s) && crashed.closes == 1,
            "and when the plugin crashes, its window is told it is gone");
    crashing.reset();
    require(crashed.closes == 1, "once");
}

// Scenario: The engine lines an isolated instrument up with the others
void compensated() {
    Song song;
    constexpr Tick ticks_per_beat = 24000; // one tick a sample at 120 BPM
    song.patterns = {{"Line up", Pattern(48000, ticks_per_beat)}};
    Trigger note;
    note.start = 4800;
    note.duration = 9600;
    note.musical_data = Note{60, 1.0F, 0.0F};
    (void)song.patterns[0].pattern.add(note);
    song.tracks.assign(2, Track{});
    song.tracks[0].name = "here";
    song.tracks[1].name = "there";
    song.tracks[0].instrument = clap_slot();
    song.tracks[1].instrument = clap_slot();
    song.clips = {{0, 0, 0, 1}, {1, 0, 0, 1}};
    SongEngine engine;
    engine.set_instrument(0, in_process(clap_slot()));
    engine.set_instrument(1, isolated(clap_slot()));
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    require(engine.output_latency() >= block, "the isolated instrument's block is compensated");
    const auto folder = std::filesystem::temp_directory_path() / "blokkily-isolation-stems";
    std::filesystem::create_directories(folder);
    const auto stems = bounce_stems(engine, song, folder, WaveFormat::float32, 0, {}, &error);
    require(stems.has_value() && stems->size() >= 2, "both tracks bounce: " + error);
    std::vector<std::vector<float>> heard;
    for (std::size_t index = 0; index < 2; ++index) {
        const auto wave = read_wave((*stems)[index].file);
        require(wave.has_value() && wave->channels == 2, "the stem reads back");
        std::vector<float> left;
        for (std::size_t frame = 0; frame < wave->frames; ++frame)
            left.push_back(wave->interleaved[frame * 2]);
        heard.push_back(std::move(left));
    }
    const auto a = first_sound(heard[0]);
    const auto b = first_sound(heard[1]);
    std::cout << "compensated: in process from " << a << ", isolated from " << b << '\n';
    require(a < heard[0].size() && a == b, "both tracks' notes land on the same sample");
    float worst = 0.0F;
    for (std::size_t index = 0; index < std::min(heard[0].size(), heard[1].size()); ++index)
        worst = std::max(worst, std::abs(heard[0][index] - heard[1][index]));
    require(worst < 1e-6F, "and sound the same");
    std::filesystem::remove_all(folder);
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "case required");
        require(sandbox_supported(), "isolation is supported here");
        const std::string which = argv[1];
        if (which == "clap") matches(clap_slot(), "clap");
        else if (which == "vst3") matches(vst3_slot(), "vst3");
        else if (which == "state") state();
        else if (which == "crash") crash();
        else if (which == "hang") hang();
        else if (which == "refused") refused();
        else if (which == "compensated") compensated();
        else if (which == "editor") editor();
        else if (which == "effect") effect();
        else if (which == "macro") macro();
        else if (which == "period") period();
        else throw std::runtime_error("unknown case");
        std::cout << "PASS " << which << '\n';
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
