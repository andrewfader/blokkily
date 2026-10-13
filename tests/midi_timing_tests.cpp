// Executable scenarios for features/midi_timing.feature. Each case is its own
// CTest test, midi_timing_<case>, and names the scenario it proves. What is
// heard comes through the production render callback (SongEngine::process)
// from the real CLAP fixture, which holds a steady level while a key is down
// and is silent otherwise. Keys come through MidiInput's own decode and route
// path, stamped with the time they arrived on a clock this file advances, so
// nothing depends on the wall clock.

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/midi/midi_input.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/sequencer/take.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

constexpr double rate = 48000.0;
// One period of the test clock: the time a device block takes. The engine
// places input by where in the period it arrived, so only fractions of it
// matter; 512000 ns makes a sample of a 512-sample block a whole 1000 ns, so
// a key placed on a sample is stamped exactly there.
constexpr std::uint64_t period = 512'000;
constexpr std::uint64_t start = 5'000'000'000;

std::atomic<std::uint64_t> now{start};
std::uint64_t test_clock() noexcept { return now.load(std::memory_order_relaxed); }

using Bytes = std::array<std::uint8_t, 3>;

// One track playing the CLAP fixture, whose clip plays pattern 0, with a
// deterministic keyboard routed to it and the engine reading the test clock.
struct Rig {
    Song song;
    SongEngine engine;
    MidiInput keyboard{MidiInput::Mode::deterministic};

    explicit Rig(std::uint32_t maximum_block) {
        song.tracks = {Track{}};
        song.clips = {{0, 0, 0, 1}};
        std::string error;
        engine.set_instrument(
            0, ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error));
        require(engine.has_instrument(0), "the CLAP fixture must load: " + error);
        require(engine.prepare(song, rate, maximum_block, 0, &error), "prepare: " + error);
        require(keyboard.open(std::size_t{0}), "the deterministic keyboard opens");
        keyboard.set_track(0);
        engine.connect_input(&keyboard.queue());
        engine.set_input_clock(&test_clock);
        now = start;
    }
    ~Rig() { engine.set_input_clock(nullptr); }

    // One device block, rendered as the clock reaches the end of the period
    // it follows. Returns its left channel.
    std::vector<float> block(std::size_t frames) {
        std::vector<float> left(frames), right(frames);
        engine.process({left, right});
        return left;
    }
    void advance() { now += period; }
    // A key message arriving `fraction` of the way through the period now
    // under way.
    void play(Bytes message, double fraction) {
        const auto at = now.load() + static_cast<std::uint64_t>(fraction * period);
        require(keyboard.inject(message, at), "the keyboard takes the message");
    }
};

// The first sample at or after `from` that sounds, or `samples.size()`.
std::size_t first_sounding(const std::vector<float>& samples, std::size_t from = 0) {
    for (std::size_t at = from; at < samples.size(); ++at)
        if (samples[at] > 0.1F) return at;
    return samples.size();
}
bool silent(const std::vector<float>& samples, std::size_t from, std::size_t to) {
    return std::all_of(samples.begin() + static_cast<std::ptrdiff_t>(from),
                       samples.begin() + static_cast<std::ptrdiff_t>(to),
                       [](float sample) { return sample == 0.0F; });
}
bool sounding(const std::vector<float>& samples, std::size_t from, std::size_t to) {
    return std::all_of(samples.begin() + static_cast<std::ptrdiff_t>(from),
                       samples.begin() + static_cast<std::ptrdiff_t>(to),
                       [](float sample) { return sample > 0.1F; });
}

// Scenario: A note is heard at the sample it was played at, one block later.
void placed_case() {
    Rig rig(512);
    require(silent(rig.block(512), 0, 512), "nothing sounds before a key goes down");
    rig.play({0x90, 60, 100}, 0.5);
    rig.advance();
    const auto pressed = rig.block(512);
    require(silent(pressed, 0, 256) && sounding(pressed, 256, 512),
            "a key half way through the period sounds from sample 256 of the next block, "
            "first sounding sample " + std::to_string(first_sounding(pressed)));
    rig.play({0x80, 60, 0}, 0.25);
    rig.advance();
    const auto released = rig.block(512);
    require(sounding(released, 0, 128) && silent(released, 128, 512),
            "a key let go a quarter of the way through stops at sample 128");
}

// Scenario: Notes played evenly are heard evenly, whatever the block size.
void spacing_case() {
    Rig rig(512);
    constexpr std::size_t frames = 512;
    constexpr std::size_t spacing = 1000;
    constexpr std::size_t first = 300;
    constexpr int notes = 8;
    // Each key, in samples of time since the clock started: down at
    // first + k * spacing, up 500 samples later.
    struct Message {
        std::size_t at;
        Bytes bytes;
    };
    std::vector<Message> messages;
    for (int note = 0; note < notes; ++note) {
        const auto down = first + static_cast<std::size_t>(note) * spacing;
        messages.push_back({down, {0x90, 60, 100}});
        messages.push_back({down + 500, {0x80, 60, 0}});
    }
    std::vector<float> heard;
    std::size_t next = 0;
    (void)rig.block(frames);   // the clock's first period begins here
    for (std::size_t block = 0; block < 20; ++block) {
        // What arrives during this period, at its own place in it.
        const std::size_t begins = block * frames;
        while (next < messages.size() && messages[next].at < begins + frames) {
            rig.play(messages[next].bytes,
                     static_cast<double>(messages[next].at - begins) / frames);
            ++next;
        }
        rig.advance();
        const auto left = rig.block(frames);
        heard.insert(heard.end(), left.begin(), left.end());
    }
    std::vector<std::size_t> onsets;
    for (std::size_t at = 1; at < heard.size(); ++at)
        if (heard[at] > 0.1F && heard[at - 1] <= 0.1F) onsets.push_back(at);
    if (heard.front() > 0.1F) onsets.insert(onsets.begin(), 0);
    std::string listed;
    for (const auto onset : onsets) listed += " " + std::to_string(onset);
    require(onsets.size() == static_cast<std::size_t>(notes),
            "every key is heard once; onsets:" + listed);
    require(onsets.front() == first, "the first note starts at sample " + std::to_string(first) +
                                         " of its block's stream; onsets:" + listed);
    for (std::size_t index = 1; index < onsets.size(); ++index)
        require(onsets[index] - onsets[index - 1] == spacing,
                "each note starts 1000 samples after the one before it; onsets:" + listed);
}

// Scenario: A note late in a long device block is placed in the chunk it
// falls in.
void chunks_case() {
    Rig rig(256);
    (void)rig.block(1024);
    rig.play({0x90, 64, 100}, 0.7);
    rig.advance();
    const auto left = rig.block(1024);
    require(silent(left, 0, 716) && sounding(left, 716, 1024),
            "a key 70% of the way through sounds from sample 716 of a 1024-sample block "
            "rendered in 256-sample chunks; first sounding sample " +
                std::to_string(first_sounding(left)));
}

// Scenario: A take records a note where it was heard.
void recorded_case() {
    Rig rig(512);
    rig.engine.set_playing(true);
    rig.engine.set_recording(true);
    rig.engine.seek(24000);
    (void)rig.block(512);   // the song is now at 24512
    rig.play({0x90, 62, 127}, 0.5);
    rig.advance();
    (void)rig.block(512);
    for (int held = 0; held < 4; ++held) {
        rig.advance();
        (void)rig.block(512);
    }
    rig.play({0x80, 62, 0}, 0.0);
    rig.advance();
    (void)rig.block(512);
    CapturedEvent captured;
    require(rig.engine.take_captured(captured) &&
                captured.event.type == PluginEvent::Type::note_on,
            "the key going down is captured");
    // 120 BPM, 480 ticks a beat at 48 kHz: 50 samples a tick.
    constexpr std::uint64_t pressed = 24512 + 256;
    require(captured.sample == pressed,
            "it is captured 256 samples into its block, at " + std::to_string(pressed) +
                ", not " + std::to_string(captured.sample));
    require(captured.tick == static_cast<Tick>(pressed / 50),
            "at that sample's tick, " + std::to_string(pressed / 50) + ", not " +
                std::to_string(captured.tick));
    const auto pressed_tick = captured.tick;
    require(rig.engine.take_captured(captured) &&
                captured.event.type == PluginEvent::Type::note_off,
            "and the key coming up");
    const auto released_tick = captured.tick;

    // Written into the pattern and played back, it starts on that tick.
    TakeRecorder take(rig.song.length());
    take.note_on(pressed_tick, 62, 1.0F, 0.0);
    const auto played = take.note_off(released_tick, 62);
    require(played.has_value(), "the take pairs the key's two messages");
    auto note = *played;
    note.start = take_target(rig.song, 0, played->start, 0).offset;
    (void)write_played(rig.song.pattern(0), note, 120);
    std::string error;
    require(rig.engine.recompile(rig.song, 0, &error), "recompile: " + error);
    rig.engine.set_recording(false);
    rig.engine.seek(0);
    std::vector<float> whole;
    while (whole.size() < 26000) {
        rig.advance();
        const auto left = rig.block(512);
        whole.insert(whole.end(), left.begin(), left.end());
    }
    const auto onset = first_sounding(whole);
    require(onset == static_cast<std::size_t>(pressed_tick) * 50,
            "played back, the note starts on its tick's sample " +
                std::to_string(pressed_tick * 50) + ", not " + std::to_string(onset));
}

// Scenario: Unstamped input sounds at the start of the next block.
void unstamped_case() {
    Rig rig(512);
    (void)rig.block(512);
    require(rig.keyboard.inject(Bytes{0x90, 60, 100}), "the keyboard takes the message");
    rig.advance();
    const auto left = rig.block(512);
    require(sounding(left, 0, 512), "it sounds from the block's first sample");
}

} // namespace

int main(int argc, char** argv) {
    const std::map<std::string, void (*)()> cases{
        {"placed", placed_case},     {"spacing", spacing_case},
        {"chunks", chunks_case},     {"recorded", recorded_case},
        {"unstamped", unstamped_case},
    };
    if (argc != 2 || !cases.contains(argv[1])) {
        std::cerr << "usage: blokkily_midi_timing_tests <case>\n";
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
