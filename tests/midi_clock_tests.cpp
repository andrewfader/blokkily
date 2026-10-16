// features/midi_clock_input.feature: following an external MIDI clock - the
// follower, the input's clock feed, and the engine's tempo override heard in
// a render.
#include "engine_graph.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/midi/clock_follower.hpp"
#include "blokkily/midi/midi_input.hpp"

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace blokkily;
namespace {

void require(bool ok, const std::string& message) {
    if (!ok) throw std::runtime_error(message);
}

// Ticks of `bpm`, `count` of them from `from_ns`, each arriving up to
// `jitter` nanoseconds early or late (a fixed pseudo-random sequence, as a
// USB port delivers a clock).
std::uint64_t feed_ticks(ClockFollower& follower, double bpm, int count, std::uint64_t from_ns,
                         std::int64_t jitter = 0) {
    const double interval = 60.0e9 / (bpm * 24.0);
    std::uint32_t seed = 12345;
    std::uint64_t at = from_ns;
    for (int i = 0; i < count; ++i) {
        seed = seed * 1664525U + 1013904223U;
        const double unit = static_cast<double>(seed >> 8) / 16777216.0 * 2.0 - 1.0;
        const auto wobble = static_cast<std::int64_t>(std::llround(unit * static_cast<double>(jitter)));
        at = static_cast<std::uint64_t>(static_cast<std::int64_t>(from_ns + jitter) +
                                        std::llround(i * interval) + wobble);
        (void)follower.feed({ClockEvent::Kind::tick, at, 0});
    }
    return at;
}

void follower_tempo() {
    ClockFollower follower;
    require(!follower.bpm(0), "no tempo before a beat of ticks");
    auto last = feed_ticks(follower, 120.0, 12, 1'000'000'000ULL);
    require(!follower.bpm(last), "half a beat is not enough to say");
    last = feed_ticks(follower, 120.0, 40, 1'000'000'000ULL);
    const auto steady = follower.bpm(last);
    require(steady && std::abs(*steady - 120.0) < 0.01, "steady ticks at 120 say 120");
    ClockFollower jittery;
    last = feed_ticks(jittery, 97.5, 48, 5'000'000'000ULL, 400'000);
    const auto wobbly = jittery.bpm(last);
    std::cout << "jittered 97.5: " << (wobbly ? *wobbly : -1.0) << '\n';
    require(wobbly && std::abs(*wobbly - 97.5) < 0.5, "a USB-jittered clock still says its tempo");
    // One late tick does not move the median.
    (void)jittery.feed({ClockEvent::Kind::tick, last + 80'000'000ULL, 0});
    const auto after_late = jittery.bpm(last + 80'000'000ULL);
    require(after_late && std::abs(*after_late - 97.5) < 0.5, "one late tick is not a tempo change");
    require(!jittery.bpm(last + 3'000'000'000ULL), "a clock silent for two seconds says nothing");
    // A pause of over a second starts measuring again.
    (void)jittery.feed({ClockEvent::Kind::tick, last + 2'000'000'000ULL, 0});
    require(!jittery.bpm(last + 2'000'000'000ULL), "after a pause the tempo is measured afresh");
}

void follower_transport() {
    ClockFollower follower;
    require(follower.feed({ClockEvent::Kind::position, 0, 32}) == ClockFollower::Move::seek &&
                follower.position() == 32, "a song position pointer is a seek, in sixteenths");
    require(follower.feed({ClockEvent::Kind::start, 0, 0}) == ClockFollower::Move::start &&
                follower.running() && follower.position() == 0, "start runs from the top");
    require(follower.feed({ClockEvent::Kind::stop, 0, 0}) == ClockFollower::Move::stop && !follower.running(),
            "stop stops");
    require(follower.feed({ClockEvent::Kind::resume, 0, 0}) == ClockFollower::Move::resume && follower.running(),
            "continue runs on");
}

void input_feed() {
    MidiInput input(MidiInput::Mode::deterministic);
    require(input.open(std::size_t{0}), "open");
    const std::array<std::uint8_t, 1> clock{0xF8}, start{0xFA}, stop{0xFC}, resume{0xFB};
    const std::array<std::uint8_t, 3> position{0xF2, 0x10, 0x01}; // 16 + 128 = 144 sixteenths
    const std::array<std::uint8_t, 3> note{0x90, 60, 100};
    require(input.inject(start, 1000) && input.inject(clock, 2000) && input.inject(note) &&
                input.inject(position, 3000) && input.inject(resume, 4000) && input.inject(stop, 5000),
            "the messages are taken");
    ClockEvent event;
    const ClockEvent::Kind expected[] = {ClockEvent::Kind::start, ClockEvent::Kind::tick, ClockEvent::Kind::position,
                                         ClockEvent::Kind::resume, ClockEvent::Kind::stop};
    for (const auto kind : expected) {
        require(input.next_clock_event(event) && event.kind == kind, "the clock arrives in order");
        if (kind == ClockEvent::Kind::position) require(event.sixteenths == 144, "the pointer's position");
        if (kind == ClockEvent::Kind::tick) require(event.time_ns == 2000, "stamped as it arrived");
    }
    require(!input.next_clock_event(event), "the note is not a clock event");
    require(input.notes_received() == 1, "the note still reaches the instrument's queue");
}

std::size_t onset(const Song& song, std::optional<double> override_bpm, std::uint64_t* samples) {
    SongEngine engine;
    engine.set_tempo_override(override_bpm);
    auto graph = populate_graph(engine, song, {}, {});
    require(graph.error.empty(), graph.error);
    std::string error;
    require(engine.prepare(song, 48000, 512, 0, &error, {}), error);
    load_fresh_state(engine, song, graph);
    *samples = engine.song_samples();
    const auto file = std::filesystem::temp_directory_path() / "blokkily-clock-onset.wav";
    require(bounce_song(engine, file, WaveFormat::float32, 0, &error).has_value(), error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value(), error);
    for (std::size_t frame = 0; frame < wave->frames; ++frame)
        if (std::abs(wave->interleaved[frame * 2]) > 1e-6F) return frame;
    throw std::runtime_error("silent");
}

void engine_override() {
    Song song;
    song.patterns = {{"Clock", Pattern(1920, 480)}};
    Trigger beat_two;
    beat_two.start = 480;
    beat_two.duration = 120;
    beat_two.musical_data = Note{60, 0.8F};
    (void)song.patterns[0].pattern.add(beat_two);
    song.tracks[0].instrument = {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", {}};
    song.clips = {{0, 0, 0, 1}};
    std::uint64_t own_length = 0, followed_length = 0;
    const auto own = onset(song, std::nullopt, &own_length);
    const auto followed = onset(song, 60.0, &followed_length);
    std::cout << "beat two: " << own << " at the song's 120, " << followed << " following 60\n";
    require(own == 24000 && followed == 48000, "beat two moves to where a clock at 60 puts it");
    require(followed_length == 2 * own_length, "the song lasts twice as long at half the tempo");
    require(song.tempo.points.size() == 1 && song.tempo.points[0].bpm == 120.0,
            "the song's own tempo map is not touched");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "case required");
        const std::string which = argv[1];
        if (which == "follower_tempo") follower_tempo();
        else if (which == "follower_transport") follower_transport();
        else if (which == "input_feed") input_feed();
        else if (which == "engine_override") engine_override();
        else throw std::runtime_error("unknown case");
        std::cout << "PASS " << which << '\n';
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
