// Executable scenarios for features/midi_output.feature: a track whose
// instrument is outboard gear, and MIDI clock. What goes out is read from a
// deterministic MidiOutput, which keeps every message it is sent with the
// engine frame it was played at, through the production render callback
// (SongEngine::process). The engine reads a test clock, so the time each
// message is due never depends on the wall clock. Each case is its own CTest
// test (midi_output_<case>).

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/midi/midi_input.hpp"
#include "blokkily/midi/midi_out_instrument.hpp"
#include "blokkily/midi/midi_output.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/project/project.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 256;
// One tick per sample: 24000 ticks a beat at 120 BPM and 48 kHz, so a clock
// pulse (a 24th of a beat) is 1000 samples.
constexpr Tick ticks_per_beat = 24000;
constexpr std::uint8_t channel = 2;   // the track's channel: MIDI channel 3

std::atomic<std::uint64_t> now{7'000'000'000};
std::uint64_t test_clock() noexcept { return now.load(std::memory_order_relaxed); }

std::string describe(const std::vector<TimedMidi>& messages) {
    std::ostringstream out;
    for (const auto& message : messages) {
        out << " [" << message.frame << ':' << std::hex;
        for (std::uint8_t byte = 0; byte < message.size; ++byte)
            out << (byte ? " " : "") << static_cast<int>(message.bytes[byte]);
        out << std::dec << ']';
    }
    return out.str();
}

// The messages whose status (with its channel) is `status`.
std::vector<TimedMidi> with_status(const std::vector<TimedMidi>& messages, std::uint8_t status) {
    std::vector<TimedMidi> found;
    for (const auto& message : messages)
        if (message.size > 0 && message.bytes[0] == status) found.push_back(message);
    return found;
}

Trigger note_at(Tick tick, Tick length, std::int16_t key, float velocity = 0.8F) {
    Trigger trigger;
    trigger.start = tick;
    trigger.duration = length;
    trigger.musical_data = Note{key, velocity};
    return trigger;
}

// One track played by a MIDI Out instrument on channel 3 of a deterministic
// output, its clip playing pattern 0.
struct Rig {
    Song song;
    std::shared_ptr<MidiOutput> output = std::make_shared<MidiOutput>(MidiOutput::Mode::deterministic);
    SongEngine engine;

    explicit Rig(Song with) : song(std::move(with)) {
        require(output->open("Deterministic output"), "the deterministic output opens");
        engine.set_instrument(0, std::make_unique<MidiOutInstrument>(output, channel));
        std::string error;
        require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
        engine.set_input_clock(&test_clock);
    }
    ~Rig() { engine.set_input_clock(nullptr); }
    void render(std::size_t frames) {
        std::vector<float> left(block), right(block);
        for (std::size_t done = 0; done < frames; done += block) {
            now += 5'333'333;   // a block's worth of time
            engine.process({left, right});
        }
    }
};

Song song_with(std::vector<Trigger> triggers, Tick length = 16384) {
    Song song;
    song.patterns = {{"Out", Pattern(length, ticks_per_beat)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};
    for (auto& trigger : triggers) (void)song.patterns[0].pattern.add(std::move(trigger));
    return song;
}

// Scenario: A track's notes are sent to outboard gear on its channel, to the sample
void notes_case() {
    Rig rig(song_with({note_at(1000, 500, 60), note_at(3000, 250, 64, 1.0F)}));
    rig.engine.set_playing(true);
    rig.render(4096);
    const auto sent = rig.output->take_sent();
    const auto on = with_status(sent, 0x92);
    const auto off = with_status(sent, 0x82);
    require(on.size() == 2 && off.size() == 2, "two notes go out on channel 3:" + describe(sent));
    require(on[0].frame == 1000 && on[0].bytes[1] == 60 && on[0].bytes[2] == 102,
            "C4 at 0.8 goes out as velocity 102 on sample 1000:" + describe(on));
    require(off[0].frame == 1500 && off[0].bytes[1] == 60, "and is let go on sample 1500");
    require(on[1].frame == 3000 && on[1].bytes[1] == 64 && on[1].bytes[2] == 127 &&
                off[1].frame == 3250,
            "E4 at full velocity from 3000 to 3250:" + describe(sent));
    // Each is due when its sample is heard: after the block before it, at
    // the sample's place in its own block.
    require(on[0].due_ns > 0 && on[1].due_ns > on[0].due_ns,
            "every message is due later than the one before it");
}

// Scenario: Controller movements go out on the track's channel
void controllers_case() {
    auto song = song_with({note_at(1000, 2000, 60)});
    song.patterns[0].pattern.add_continuous({2000, ContinuousEvent::Kind::control_change, 1, 64});
    song.patterns[0].pattern.add_continuous({2500, ContinuousEvent::Kind::pitch_bend, 0, 12288});
    Rig rig(std::move(song));
    rig.engine.set_playing(true);
    rig.render(4096);
    const auto sent = rig.output->take_sent();
    const auto wheel = with_status(sent, 0xB2);
    const auto bend = with_status(sent, 0xE2);
    const bool moved = std::any_of(wheel.begin(), wheel.end(), [](const TimedMidi& message) {
        return message.frame == 2000 && message.bytes[1] == 1 && message.bytes[2] == 64;
    });
    require(moved, "the mod wheel to 64 goes out as CC 1 on channel 3 at sample 2000:" +
                       describe(sent));
    const bool bent = std::any_of(bend.begin(), bend.end(), [](const TimedMidi& message) {
        return message.frame == 2500 && message.bytes[1] == 0 && message.bytes[2] == 96;
    });
    require(bent, "the bend goes out at sample 2500, in two 7-bit halves:" + describe(sent));
}

// Scenario: Keys played into the track go out on its channel
void live_case() {
    Rig rig(song_with({}));
    MidiInput keyboard(MidiInput::Mode::deterministic);
    require(keyboard.open(std::size_t{0}), "the deterministic keyboard opens");
    keyboard.set_track(0);
    rig.engine.connect_input(&keyboard.queue());
    rig.render(512);
    require(keyboard.inject(std::array<std::uint8_t, 3>{0x90, 67, 100}), "key down");
    rig.render(256);
    require(keyboard.inject(std::array<std::uint8_t, 3>{0x80, 67, 0}), "key up");
    rig.render(256);
    const auto sent = rig.output->take_sent();
    require(with_status(sent, 0x92).size() == 1 && with_status(sent, 0x82).size() == 1 &&
                with_status(sent, 0x92)[0].bytes[1] == 67,
            "the key played on channel 1 goes out on the track's channel 3:" + describe(sent));
}

// Scenario: Stopping the song lets go of every note it struck
void stop_case() {
    Rig rig(song_with({note_at(1000, 8000, 60)}));
    rig.engine.set_playing(true);
    rig.render(2048);
    rig.engine.set_playing(false);
    rig.render(256);
    const auto sent = rig.output->take_sent();
    require(with_status(sent, 0x92).size() == 1 && with_status(sent, 0x82).size() == 1 &&
                with_status(sent, 0x82)[0].frame == 2048,
            "the held note is let go on the first sample after the stop:" + describe(sent));
}

// Scenario: MIDI clock follows the song
void clock_case() {
    Rig rig(song_with({}));
    auto clock = std::make_shared<MidiOutput>(MidiOutput::Mode::deterministic);
    require(clock->open("Deterministic output"), "the clock's output opens");
    rig.engine.set_clock_output(clock.get());
    rig.render(512);
    require(clock->take_sent().empty(), "a stopped song sends no clock");

    rig.engine.set_playing(true);
    rig.render(4352);   // song samples 0 to 4352, engine frames 512 to 4864
    auto sent = clock->take_sent();
    require(!sent.empty() && sent.front().bytes[0] == 0xFA && sent.front().frame == 512,
            "playing from the start sends Start first:" + describe(sent));
    const auto pulses = with_status(sent, 0xF8);
    require(pulses.size() == 5, "a pulse every 1000 samples, five in 4352:" + describe(sent));
    for (std::size_t index = 0; index < pulses.size(); ++index)
        require(pulses[index].frame == 512 + 1000 * index,
                "pulse " + std::to_string(index) + " on song sample " +
                    std::to_string(1000 * index) + ":" + describe(pulses));

    rig.engine.set_playing(false);
    rig.render(256);
    sent = clock->take_sent();
    require(sent.size() == 1 && sent[0].bytes[0] == 0xFC, "stopping sends Stop:" + describe(sent));

    // Played again from song sample 6000: Song Position (one sixteenth in,
    // 6000 of a beat's 24000 ticks) and Continue, then pulses from 6000.
    rig.engine.seek(6000);
    rig.engine.set_playing(true);
    rig.render(2048);
    sent = clock->take_sent();
    require(sent.size() >= 2 && sent[0].bytes[0] == 0xF2 && sent[0].bytes[1] == 1 &&
                sent[0].bytes[2] == 0 && sent[1].bytes[0] == 0xFB,
            "played from elsewhere, Song Position 1 and Continue:" + describe(sent));
    const auto resumed = with_status(sent, 0xF8);
    require(resumed.size() == 3 && resumed[0].frame == sent[0].frame &&
                resumed[1].frame == resumed[0].frame + 1000,
            "and a pulse on sample 6000 and every 1000 after:" + describe(sent));
}

// Scenario: An export sends nothing to outboard gear
void bounce_case() {
    Rig rig(song_with({note_at(1000, 500, 60)}));
    auto clock = std::make_shared<MidiOutput>(MidiOutput::Mode::deterministic);
    require(clock->open("Deterministic output"), "the clock's output opens");
    rig.engine.set_clock_output(clock.get());
    const auto file = std::filesystem::temp_directory_path() / "blokkily-midi-out-bounce.wav";
    std::string error;
    const auto report = bounce_song(rig.engine, file, WaveFormat::float32, 0, &error);
    std::filesystem::remove(file);
    require(report.has_value(), "the bounce is written: " + error);
    require(rig.output->take_sent().empty() && clock->take_sent().empty(),
            "nothing is sent while the export renders");
    require(!rig.engine.hardware_midi_muted(), "and hardware MIDI is unmuted afterwards");
    rig.engine.set_playing(true);
    rig.render(2048);
    require(with_status(rig.output->take_sent(), 0x92).size() == 1,
            "the song is sent again when it plays");
}

// Scenario: The track's channel is the instrument's state, saved with the song
void state_case() {
    auto output = std::make_shared<MidiOutput>(MidiOutput::Mode::deterministic);
    require(output->open("Deterministic output"), "the output opens");
    MidiOutInstrument instrument(output, 9);
    const auto saved = instrument.save_state();
    require(saved.size() == 1 && static_cast<int>(saved[0]) == 9, "the channel is its state");
    MidiOutInstrument other(output, 0);
    require(other.load_state(saved) && other.channel() == 9, "and loads back");
    require(!other.load_state(std::vector<std::byte>{std::byte{16}}),
            "a channel beyond 16 is refused");
    // In a project the slot keeps the port and the channel.
    Project project;
    project.song = song_with({});
    project.song.tracks[0].instrument = {midi_out_format, "", "Synth Port 1",
                                         std::vector<std::byte>{std::byte{9}}};
    const auto text = ProjectFile::serialize(project);
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value(), "the project parses back: " + error);
    const auto& slot = loaded->song.tracks[0].instrument;
    require(slot.format == midi_out_format && slot.identifier == "Synth Port 1" &&
                slot.state == std::vector<std::byte>{std::byte{9}},
            "the MIDI Out slot comes back with its port and channel");
    // Going away, it lets go of everything on its channel.
    (void)output->take_sent();
    {
        MidiOutInstrument leaving(output, 4);
    }
    const auto sent = output->take_sent();
    require(sent.size() == 1 && sent[0].bytes[0] == 0xB4 && sent[0].bytes[1] == 123,
            "a MIDI Out instrument that goes sends All Notes Off on its channel:" + describe(sent));
}

} // namespace

int main(int argc, char** argv) {
    const std::map<std::string, void (*)()> cases{
        {"notes", notes_case}, {"controllers", controllers_case}, {"live", live_case},
        {"stop", stop_case},   {"clock", clock_case},             {"bounce", bounce_case},
        {"state", state_case},
    };
    if (argc != 2 || !cases.contains(argv[1])) {
        std::cerr << "usage: blokkily_midi_output_tests <case>\n";
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
