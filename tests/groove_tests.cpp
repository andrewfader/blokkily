// features/quantize_humanize.feature: quantize and humanize a part, proved on
// the pattern and, for timing, from the audio the engine renders.
#include "engine_graph.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/model/groove.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace blokkily;
namespace {

constexpr Tick step = 120;

void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

Trigger note_at(Tick start, Tick micro, std::int16_t key = 60, float velocity = 0.8F) {
    Trigger trigger;
    trigger.start = start;
    trigger.micro_offset = micro;
    trigger.duration = 60;
    trigger.musical_data = Note{key, velocity};
    return trigger;
}

const Trigger* at_start(const Pattern& pattern, Tick start) {
    for (const auto& trigger : pattern.events())
        if (trigger.start == start) return &trigger;
    return nullptr;
}

void quantize_full() {
    Pattern pattern(1920, 480);
    (void)pattern.add(note_at(0, 30));
    (void)pattern.add(note_at(4 * step, -40));
    (void)pattern.add(note_at(9 * step, 0));
    auto sixteenths = pattern;
    const auto result = quantize_pattern(sixteenths, step, 1.0, step);
    require(result.changed == 2 && result.kept == 0, "two played-off steps move onto the grid");
    for (const auto& trigger : sixteenths.events())
        require(trigger.micro_offset == 0, "every step sits on the sixteenth grid");
    require(at_start(sixteenths, 4 * step) != nullptr, "a step played early stays on its own step");
    // On eighths the odd sixteenth moves to the eighth after it.
    auto eighths = pattern;
    (void)quantize_pattern(eighths, 2 * step, 1.0, step);
    require(at_start(eighths, 9 * step) == nullptr && at_start(eighths, 10 * step) != nullptr,
            "the sixteenth at step 9 moves to the eighth at step 10");
}

void quantize_strength() {
    Pattern pattern(1920, 480);
    (void)pattern.add(note_at(4 * step, 40));
    (void)quantize_pattern(pattern, step, 0.5, step);
    require(pattern.events()[0].micro_offset == 20, "half strength halves how far off it is");
    (void)quantize_pattern(pattern, step, 0.0, step);
    require(pattern.events()[0].micro_offset == 20, "no strength moves nothing");
}

void quantize_collisions() {
    Pattern pattern(1920, 480);
    (void)pattern.add(note_at(8 * step, 0, 60));
    (void)pattern.add(note_at(9 * step, 0, 62));
    (void)pattern.add(note_at(15 * step, 50, 64));
    const auto result = quantize_pattern(pattern, 4 * step, 1.0, step);
    require(result.kept == 1, "the step that would land on a taken one is kept");
    require(at_start(pattern, 9 * step) != nullptr && at_start(pattern, 8 * step) != nullptr,
            "neither the kept step nor the one it would have hit moves");
    const auto* wrapped = at_start(pattern, 0);
    require(wrapped != nullptr && std::get<Note>(wrapped->musical_data).key == 64 &&
                wrapped->micro_offset == 0,
            "a step played just before the loop's end lands on its downbeat");
}

void humanize() {
    Pattern pattern(1920, 480);
    for (Tick at = 0; at < 1920; at += step) (void)pattern.add(note_at(at, 0, 60, 0.5F));
    auto first = pattern, second = pattern, other = pattern;
    const auto result = humanize_pattern(first, 8, 0.1F, 42, step);
    (void)humanize_pattern(second, 8, 0.1F, 42, step);
    (void)humanize_pattern(other, 8, 0.1F, 43, step);
    require(result.changed == 16, "every step is loosened");
    bool moved = false, louder = false, softer = false, same = true, differs = false;
    for (std::size_t i = 0; i < first.events().size(); ++i) {
        const auto& a = first.events()[i];
        const auto& b = second.events()[i];
        const auto& c = other.events()[i];
        const float velocity = std::get<Note>(a.musical_data).velocity;
        require(std::abs(a.micro_offset) <= 8, "timing moves at most 8 ticks");
        require(std::abs(velocity - 0.5F) <= 0.1F + 1e-6F, "velocity moves at most 0.1");
        require(a.start == pattern.events()[i].start, "every step stays on its step");
        moved = moved || a.micro_offset != 0;
        louder = louder || velocity > 0.5F;
        softer = softer || velocity < 0.5F;
        same = same && a.micro_offset == b.micro_offset &&
               std::get<Note>(b.musical_data).velocity == velocity;
        differs = differs || a.micro_offset != c.micro_offset;
    }
    require(moved && louder && softer, "it moves timing, and velocity both ways");
    require(same, "the same seed loosens it the same way");
    require(differs, "another seed loosens it differently");
}

// Where the first sound of a bounce is, in samples.
std::size_t onset(const Song& song, const std::filesystem::path& file) {
    SongEngine engine;
    auto graph = populate_graph(engine, song, {}, {});
    require(graph.error.empty(), graph.error);
    std::string error;
    require(engine.prepare(song, 48000, 512, 0, &error, {}), error);
    load_fresh_state(engine, song, graph);
    require(bounce_song(engine, file, WaveFormat::float32, 0, &error).has_value(), error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value(), error);
    for (std::size_t frame = 0; frame < wave->frames; ++frame)
        if (std::abs(wave->interleaved[frame * 2]) > 1e-6F) return frame;
    throw std::runtime_error("the bounce is silent");
}

void render() {
    // A note played 40 ticks late on beat 2, at 120 BPM: 50 samples a tick.
    Song song;
    song.patterns = {{"Groove", Pattern(1920, 480)}};
    (void)song.patterns[0].pattern.add(note_at(4 * step, 40));
    song.tracks[0].instrument = {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", {}};
    song.clips = {{0, 0, 0, 1}};
    const auto directory = std::filesystem::temp_directory_path() / "blokkily-groove-tests";
    std::filesystem::create_directories(directory);
    const auto late = onset(song, directory / "late.wav");
    (void)quantize_pattern(song.patterns[0].pattern, step, 1.0, step);
    const auto quantized = onset(song, directory / "quantized.wav");
    std::cout << "onset " << late << " played, " << quantized << " quantized\n";
    require(late == 26000, "the late note sounds 40 ticks after beat 2");
    require(quantized == 24000, "quantized, it sounds on beat 2 exactly");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "case required");
        const std::string which = argv[1];
        if (which == "quantize_full") quantize_full();
        else if (which == "quantize_strength") quantize_strength();
        else if (which == "quantize_collisions") quantize_collisions();
        else if (which == "humanize") humanize();
        else if (which == "render") render();
        else throw std::runtime_error("unknown case");
        std::cout << "PASS " << which << '\n';
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
