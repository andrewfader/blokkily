// features/arpeggiator.feature: the arpeggiator stage on its own, and in the
// production SongEngine with the CLAP fixture, read from the rendered audio.
#include "audio/engine/engine_arpeggiator.hpp"
#include "engine_graph.hpp"
#include "support/audio_probe.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/project/project.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <vector>

using namespace blokkily;
namespace {

void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

constexpr std::size_t step = 6000; // a sixteenth at 120 BPM, 48 kHz

PluginEvent on(std::uint32_t at, int key, double velocity = 0.8) {
    return {PluginEvent::Type::note_on, at, key, velocity, 0.0, 0};
}
PluginEvent off(std::uint32_t at, int key) { return {PluginEvent::Type::note_off, at, key, 0.0, 0.0, 0}; }

struct Played {
    std::uint64_t at;
    bool on;
    int key;
};

// Runs `input` (absolute samples) through an arpeggiator set to `settings`
// in chunks of `chunk`, from song position 0, and returns what comes out.
std::vector<Played> run(const Arpeggiator& settings, const std::vector<std::pair<std::uint64_t, PluginEvent>>& input,
                        std::size_t frames, std::size_t chunk, bool rolling = true,
                        std::uint64_t release_at = UINT64_MAX) {
    engine::ArpPlayback arp;
    engine::set_arpeggiator(arp, settings);
    const TickClock clock;
    std::vector<PluginEvent> events(512), scratch(512);
    std::vector<Played> played;
    for (std::uint64_t start = 0; start < frames; start += chunk) {
        const auto length = std::min<std::uint64_t>(chunk, frames - start);
        std::size_t count = 0;
        for (const auto& [at, event] : input)
            if (at >= start && at < start + length) {
                events[count] = event;
                events[count++].sample_offset = static_cast<std::uint32_t>(at - start);
            }
        const bool release = release_at >= start && release_at < start + length;
        count = engine::run_arpeggiator(arp, {start, static_cast<std::size_t>(length), rolling, release,
                                              &clock, 480, 48000.0},
                                        events, count, scratch);
        for (std::size_t i = 0; i < count; ++i)
            if (events[i].type == PluginEvent::Type::note_on || events[i].type == PluginEvent::Type::note_off)
                played.push_back({start + events[i].sample_offset,
                                  events[i].type == PluginEvent::Type::note_on, events[i].key_or_parameter});
    }
    return played;
}

std::vector<int> keys_on(const std::vector<Played>& played) {
    std::vector<int> keys;
    for (const auto& event : played)
        if (event.on) keys.push_back(event.key);
    return keys;
}

Arpeggiator arp_on(Arpeggiator::Mode mode = Arpeggiator::Mode::up) {
    Arpeggiator arp;
    arp.enabled = true;
    arp.mode = mode;
    return arp;
}

const std::vector<std::pair<std::uint64_t, PluginEvent>> chord{
    {0, on(0, 64)}, {0, on(0, 60)}, {0, on(0, 67)}};

void stage_up() {
    const auto played = run(arp_on(), chord, 24000, 24000);
    const std::vector<Played> expected{{0, true, 60},      {3000, false, 60},  {6000, true, 64},
                                       {9000, false, 64},  {12000, true, 67},  {15000, false, 67},
                                       {18000, true, 60},  {21000, false, 60}};
    require(played.size() == expected.size(), "a held chord plays one note a sixteenth, half a step long");
    for (std::size_t i = 0; i < expected.size(); ++i)
        require(played[i].at == expected[i].at && played[i].on == expected[i].on &&
                    played[i].key == expected[i].key,
                "event " + std::to_string(i) + " is " + std::to_string(played[i].key) + " at " +
                    std::to_string(played[i].at));
    // The same, chunk by chunk as a device asks for it.
    const auto chunked = run(arp_on(), chord, 24000, 256);
    require(chunked.size() == played.size(), "chunks change nothing");
    for (std::size_t i = 0; i < played.size(); ++i)
        require(chunked[i].at == played[i].at && chunked[i].key == played[i].key, "chunks change nothing");
}

void stage_modes() {
    require(keys_on(run(arp_on(Arpeggiator::Mode::down), chord, 24000, 512)) == std::vector<int>{67, 64, 60, 67},
            "down walks from the top");
    auto two = arp_on(Arpeggiator::Mode::up_down);
    two.octaves = 2;
    require(keys_on(run(two, chord, 60000, 512)) == std::vector<int>{60, 64, 67, 72, 76, 79, 76, 72, 67, 64},
            "up and down over two octaves turns at the ends without repeating them");
    const std::vector<std::pair<std::uint64_t, PluginEvent>> pressed{
        {0, on(0, 67)}, {1, on(0, 60)}, {2, on(0, 64)}};
    require(keys_on(run(arp_on(Arpeggiator::Mode::played), pressed, 24000, 512)) ==
                std::vector<int>{67, 60, 64, 67},
            "played order keeps the order the keys went down");
    const auto random = keys_on(run(arp_on(Arpeggiator::Mode::random), chord, 96000, 512));
    require(random == keys_on(run(arp_on(Arpeggiator::Mode::random), chord, 96000, 256)),
            "random is the same every time");
    std::map<int, int> heard;
    for (const int key : random) heard[key] += 1;
    require(heard.size() == 3 && heard.count(60) && heard.count(64) && heard.count(67),
            "random plays every held key and nothing else");
    auto eighths = arp_on();
    eighths.division = 2;
    eighths.gate = 1.0F;
    const auto slow = run(eighths, chord, 24000, 512);
    require(keys_on(slow) == std::vector<int>{60, 64}, "eighths step every 12000 samples");
    require(slow[1].at == 12000 && !slow[1].on && slow[2].at == 12000 && slow[2].on,
            "a full gate ends one note where the next begins");
}

void stage_release_and_latch() {
    auto input = chord;
    for (const int key : {60, 64, 67}) input.push_back({7000, off(0, key)});
    const auto free_play = run(arp_on(), input, 24000, 512);
    require(keys_on(free_play) == std::vector<int>{60, 64}, "let go, the arpeggio stops");
    require(!free_play.back().on && free_play.back().at == 7000, "and its note ends when the keys do");
    auto latched = arp_on();
    latched.latch = true;
    require(keys_on(run(latched, input, 24000, 512)) == std::vector<int>{60, 64, 67, 60},
            "latched, it keeps going after the keys are let go");
    auto next = input;
    next.push_back({13000, on(0, 50)});
    require(keys_on(run(latched, next, 24000, 512)) == std::vector<int>{60, 64, 67, 50, 50},
            "a new key after a full release replaces the latched chord, starting at once");
    const auto stopped = run(latched, input, 24000, 512, true, 14000);
    require(keys_on(stopped) == std::vector<int>{60, 64, 67}, "a stop lets the latch go");
    // Stopped, it keeps its own time from the first key.
    const std::vector<std::pair<std::uint64_t, PluginEvent>> late{{1000, on(0, 60)}, {1000, on(0, 64)}};
    const auto free_running = run(arp_on(), late, 24000, 512, false);
    require(keys_on(free_running).size() >= 4 && free_running[0].at == 1000,
            "with the transport stopped it starts on the key and keeps stepping");
    // A note-off for a key it never took goes through to the instrument.
    const std::vector<std::pair<std::uint64_t, PluginEvent>> stray{{100, off(0, 72)}};
    const auto passed = run(arp_on(), stray, 1000, 512);
    require(passed.size() == 1 && !passed[0].on && passed[0].key == 72, "a stray note-off passes");
}

// --- In the engine --------------------------------------------------------

Song chord_song(bool arpeggiated) {
    Song song;
    song.patterns = {{"Arp", Pattern(1920, 480)}};
    for (const std::int16_t key : {60, 64, 67}) {
        Trigger trigger;
        trigger.duration = 1920;
        trigger.musical_data = Note{key, 0.9F};
        // The fixture's oscillator, so each key is a pitch.
        trigger.locks = {{"tone", 1, 1.0, ParameterLock::Kind::automation}};
        (void)song.patterns[0].pattern.add(trigger);
    }
    song.tracks[0].instrument = {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", {}};
    song.tracks[0].arp.enabled = arpeggiated;
    song.clips = {{0, 0, 0, 1}};
    return song;
}

std::vector<float> bounce(const Song& song, const std::string& name) {
    SongEngine engine;
    auto graph = populate_graph(engine, song, {}, {});
    require(graph.error.empty(), graph.error);
    std::string error;
    require(engine.prepare(song, 48000, 512, 0, &error, {}), error);
    load_fresh_state(engine, song, graph);
    const auto file = std::filesystem::temp_directory_path() / ("blokkily-arp-" + name + ".wav");
    require(bounce_song(engine, file, WaveFormat::float32, 0, &error).has_value(), error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value(), error);
    std::vector<float> left(wave->frames);
    for (std::size_t frame = 0; frame < left.size(); ++frame) left[frame] = wave->interleaved[frame * 2];
    return left;
}

double hz(int key) { return 440.0 * std::pow(2.0, (key - 69) / 12.0); }

void engine_render() {
    const auto held = bounce(chord_song(false), "chord");
    const auto arpeggiated = bounce(chord_song(true), "arp");
    const auto energy = [](const std::vector<float>& samples, std::size_t from, int key) {
        return probe::goertzel_energy(std::span<const float>(samples).subspan(from, 2400), 48000.0, hz(key));
    };
    // Unarpeggiated, the (monophonic) fixture holds its note through the
    // whole step: nothing goes silent half way.
    for (std::size_t s = 0; s < 8; ++s)
        require(probe::peak(std::span<const float>(held).subspan(s * step + 3100, 2800)) > 0.01F,
                "unarpeggiated, the held note sounds through every step");
    // Arpeggiated, each sixteenth's first half is one key of it, in order,
    // and its second half is silent.
    const int expected[] = {60, 64, 67, 60, 64, 67, 60, 64};
    for (std::size_t s = 0; s < 8; ++s) {
        const auto from = s * step + 300;
        const int key = expected[s];
        std::cout << "step " << s << ":";
        for (const int other : {60, 64, 67}) std::cout << ' ' << energy(arpeggiated, from, other);
        std::cout << '\n';
        require(energy(arpeggiated, from, key) > 1e-4, "step " + std::to_string(s) + " sounds its key");
        for (const int other : {60, 64, 67})
            if (other != key)
                require(energy(arpeggiated, from, other) < energy(arpeggiated, from, key) * 0.01,
                        "step " + std::to_string(s) + " sounds that key alone");
        require(probe::peak(std::span<const float>(arpeggiated).subspan(s * step + 3100, 2800)) < 1e-6F,
                "the second half of step " + std::to_string(s) + " is silent (gate 0.5)");
    }
}

void records() {
    Project project;
    project.song = chord_song(true);
    auto& arp = project.song.tracks[0].arp;
    arp.mode = Arpeggiator::Mode::up_down;
    arp.division = 6;
    arp.octaves = 3;
    arp.gate = 0.75F;
    arp.latch = true;
    const auto text = ProjectFile::serialize(project);
    require(text.find("arp 0 on updown 6 3 0.75 latch") != std::string::npos, "the arpeggiator is saved");
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value(), error);
    require(loaded->song.tracks[0].arp == arp, "it loads as it was saved");
    Project plain;
    require(ProjectFile::serialize(plain).find("arp ") == std::string::npos,
            "a song that never used one saves no record");
    auto bad = text;
    bad.replace(bad.find("arp 0 on updown 6"), 17, "arp 0 on updown 5");
    require(!ProjectFile::parse(bad, &error).has_value(), "a division it does not have is refused");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "case required");
        const std::string which = argv[1];
        if (which == "stage_up") stage_up();
        else if (which == "stage_modes") stage_modes();
        else if (which == "stage_release_and_latch") stage_release_and_latch();
        else if (which == "engine_render") engine_render();
        else if (which == "records") records();
        else throw std::runtime_error("unknown case");
        std::cout << "PASS " << which << '\n';
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
