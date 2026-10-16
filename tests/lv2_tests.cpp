// features/lv2_hosting.feature: a real .lv2 bundle built by the suite, found
// by the scan, made through the production factory and JUCE's LV2 host,
// played with sample-offset notes, moved by its parameter, and saved and
// loaded as state; and a song of it bounced through the engine.
#include "engine_graph.hpp"
#include "processor_factory.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/plugins/plugin_scan.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace blokkily;
namespace fs = std::filesystem;
namespace {

void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

const fs::path bundle{BLOKKILY_TEST_LV2_BUNDLE};
constexpr const char* uri = "urn:blokkily:test-lv2";

void scan() {
    const auto found = Vst3PluginInstance::scan_lv2(bundle);
    require(found.size() == 1, "the bundle holds one plugin, found " + std::to_string(found.size()));
    require(found[0].name == "Blokkily Test LV2" && found[0].identifier == uri && found[0].instrument,
            "it is named, has its URI, and is an instrument: " + found[0].name + " " + found[0].identifier);
    // The scan finds the bundle in its folder, as it finds installed ones.
    const auto candidates = enumerate_scan_candidates({}, {}, {bundle.parent_path()});
    bool listed = false;
    for (const auto& candidate : candidates)
        listed = listed || (candidate.format == "LV2" && fs::equivalent(candidate.path, bundle));
    require(listed, "the folder's .lv2 bundle is a scan candidate");
    std::string error;
    const auto records = scan_candidate({"LV2", bundle}, &error);
    require(records.size() == 1 && records[0].format == "LV2" && records[0].identifier == uri &&
                records[0].kind == "instrument", "the helper's record names it: " + error);
}

std::vector<float> run(PluginInstance& plugin, const std::vector<PluginEvent>& events, std::size_t frames = 512) {
    std::vector<float> left(frames, -1.0F), right(frames, -1.0F);
    plugin.process({left, right}, events);
    return left;
}

void process() {
    std::string error;
    auto plugin = Vst3PluginInstance::create_lv2(bundle, uri, &error);
    require(plugin != nullptr, "the LV2 plugin is made through JUCE: " + error);
    require(plugin->format() == "LV2", "it says it is LV2");
    require(plugin->activate(48000.0, 1, 512), "it activates");
    const auto out = run(*plugin, {{PluginEvent::Type::note_on, 100, 60, 1.0, 0.0, 0},
                                   {PluginEvent::Type::note_off, 300, 60, 0.0, 0.0, 0}});
    std::cout << "lv2: " << out[99] << " before, " << out[100] << " from the note, " << out[300] << " after\n";
    require(out[99] == 0.0F, "silent before the note's sample");
    require(std::abs(out[100] - 0.25F) < 1e-4F && std::abs(out[299] - 0.25F) < 1e-4F,
            "Level x velocity from the note's sample to its release");
    require(out[300] == 0.0F, "silent from the release's sample");
}

void parameters_and_state() {
    std::string error;
    auto plugin = Vst3PluginInstance::create_lv2(bundle, uri, &error);
    require(plugin && plugin->activate(48000.0, 1, 512), error);
    int level = -1;
    for (const auto& parameter : plugin->parameters())
        if (parameter.name == "Level") level = parameter.id;
    require(level >= 0, "its Level parameter is listed");
    const auto out = run(*plugin, {{PluginEvent::Type::parameter_value, 0, level, 0.75, 0.0, 0},
                                   {PluginEvent::Type::note_on, 10, 60, 1.0, 0.0, 0}});
    require(std::abs(out[100] - 0.75F) < 1e-3F, "a Level of 0.75 is heard");
    const auto state = plugin->save_state();
    require(!state.empty(), "it saves its state");
    auto other = Vst3PluginInstance::create_lv2(bundle, uri, &error);
    require(other && other->activate(48000.0, 1, 512) && other->load_state(state), "another loads it");
    const auto again = run(*other, {{PluginEvent::Type::note_on, 0, 60, 1.0, 0.0, 0}});
    require(std::abs(again[100] - 0.75F) < 1e-3F, "the loaded state is heard");
}

void factory_and_engine() {
    const InstrumentSlot slot{"LV2", bundle.string(), uri, {}};
    std::string error;
    require(create_processor(slot, {}, &error) != nullptr, "the factory makes it: " + error);
    Song song;
    song.patterns = {{"LV2", Pattern(1920, 480)}};
    Trigger trigger;
    trigger.start = 480;
    trigger.duration = 240;
    trigger.musical_data = Note{60, 1.0F};
    (void)song.patterns[0].pattern.add(trigger);
    song.tracks[0].instrument = slot;
    song.clips = {{0, 0, 0, 1}};
    SongEngine engine;
    auto graph = populate_graph(engine, song, {}, {});
    require(graph.error.empty(), graph.error);
    require(engine.prepare(song, 48000, 512, 0, &error, {}), error);
    load_fresh_state(engine, song, graph);
    const auto file = fs::temp_directory_path() / "blokkily-lv2.wav";
    require(bounce_song(engine, file, WaveFormat::float32, 0, &error).has_value(), error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value(), error);
    const auto at = [&](std::size_t frame) { return wave->interleaved[2 * frame] / 0.70710678F; };
    std::cout << "lv2 in the engine: " << at(23999) << " then " << at(24000) << '\n';
    require(at(23999) == 0.0F && std::abs(at(24000) - 0.25F) < 1e-4F,
            "the bounce sounds it from beat two's sample exactly");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "case required");
        const std::string which = argv[1];
        if (which == "scan") scan();
        else if (which == "process") process();
        else if (which == "parameters_and_state") parameters_and_state();
        else if (which == "factory_and_engine") factory_and_engine();
        else throw std::runtime_error("unknown case");
        std::cout << "PASS " << which << '\n';
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
