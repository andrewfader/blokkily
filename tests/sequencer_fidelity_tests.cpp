// Executable scenarios for features/sequencer_fidelity.feature: what a tracker
// or DAW user expects of the sequencer, the engine and the files, each proved
// in rendered audio, scheduled events or bytes read back. Each case is its own
// CTest test, sequencer_fidelity_<case>. Audio claims are read off audio
// rendered through the production render callback by the real CLAP fixture,
// which sounds DC while a note is held.

#include "support/audio_probe.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/event_timeline.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/effects/builtin.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/project/midi_file.hpp"
#include "blokkily/project/project.hpp"
#include "blokkily/sequencer/scheduler.hpp"
#include "blokkily/sequencer/take.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

// 120 BPM, 480 ticks a beat, 48 kHz: one tick is 50 samples, one bar 96000.
constexpr double rate = 48000.0;
constexpr std::uint32_t block = 512;
constexpr std::uint64_t bar_samples = 96000;

// Drives the production render callback, the way a device does.
class Pump {
public:
    explicit Pump(AudioSource& source) : output_(RtAudioOutput::Mode::deterministic) {
        require(output_.open(source) && output_.start(), "the production output must start");
    }
    std::vector<float> left(std::size_t frames, std::size_t block_size = block) {
        std::vector<float> result;
        result.reserve(frames);
        std::vector<float> stereo;
        while (result.size() < frames) {
            const auto now = std::min(block_size, frames - result.size());
            stereo.assign(now * 2, -1.0F);
            require(output_.pump(stereo), "the production callback must render");
            result.insert(result.end(), stereo.begin(), stereo.begin() + static_cast<long>(now));
        }
        return result;
    }

private:
    RtAudioOutput output_;
};

std::unique_ptr<PluginInstance> clap_fixture() {
    std::string error;
    auto instance = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
    require(instance != nullptr, "the CLAP fixture must load through the production adapter: " + error);
    return instance;
}

Trigger note_at(Tick start, Tick duration, int key = 60) {
    Trigger trigger;
    trigger.start = start;
    trigger.duration = duration;
    trigger.musical_data = Note{static_cast<std::int16_t>(key), 1.0F, 0.0F};
    return trigger;
}

// A song of one track, hard left so the left channel is the track at unity.
Song song_of(Pattern pattern, std::vector<Clip> clips = {{0, 0, 0, 1}}) {
    Song song;
    song.patterns = {{"Fidelity", std::move(pattern)}};
    song.tracks = {Track{}};
    song.tracks[0].mix.pan = -1.0;
    song.clips = std::move(clips);
    return song;
}

std::vector<std::pair<Tick, int>> keys_of(const ScheduledEvents& events) {
    std::vector<std::pair<Tick, int>> keys;
    for (const auto& note : events.notes) keys.emplace_back(note.start, note.key);
    std::sort(keys.begin(), keys.end());
    return keys;
}

// ---- The scheduler -----------------------------------------------------------

// Scenario: A key struck again while it still sounds hands over to the new note
void overlap_hands_over() {
    Pattern pattern(1920, 480);
    (void)pattern.add(note_at(0, 480));
    (void)pattern.add(note_at(120, 480));
    const auto song = song_of(std::move(pattern));
    SongEngine engine;
    engine.set_instrument(0, clap_fixture());
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    engine.set_playing(true);
    Pump pump(engine);
    const auto audio = pump.left(engine.song_samples());
    // The second note lasts until tick 600 (sample 30000): the first note's
    // release, which used to land at tick 480 and cut it, now comes at tick
    // 120, where the second note starts.
    require(audio[100] > 0.1F, "the first note sounds");
    require(audio[6000 + 100] > 0.1F, "the second note sounds");
    require(audio[24000 + 100] > 0.1F, "the second note is not cut at the first note's end");
    require(audio[29900] > 0.1F && audio[30001] == 0.0F,
            "the second note ends where it was written to end");
}

// Scenario: A pattern placed again where it just ended plays its next loop
void loop_numbers_continue() {
    Pattern pattern(1920, 480);
    auto every_other = note_at(0, 120);
    every_other.play_on_loop = 2;
    (void)pattern.add(every_other);
    auto second_only = note_at(480, 120, 62);
    second_only.play_on_loop = 2;
    (void)pattern.add(second_only);
    const auto repeated = song_of(pattern, {{0, 0, 0, 4}});
    const auto placed = song_of(pattern, {{0, 0, 0, 1}, {0, 0, 1920, 1}, {0, 0, 3840, 1},
                                          {0, 0, 5760, 1}});
    const auto from_repeats = keys_of(repeated.arrange(0));
    const auto from_placements = keys_of(placed.arrange(0));
    require(from_repeats.size() == 4, "a step that plays every other loop plays twice in four");
    require(from_placements == from_repeats,
            "four placements in a row play what one clip repeated four times plays");
    // A gap between placements starts the count again.
    const auto gapped = song_of(pattern, {{0, 0, 0, 1}, {0, 0, 3840, 1}});
    require(keys_of(gapped.arrange(0)).size() == 4,
            "a placement after a gap is the pattern's first loop again");
}

// Scenario: An inversion moves whole voices by octaves, below as above
void inversion_octaves() {
    const Scheduler scheduler;
    const auto voiced = [&scheduler](std::int8_t inversion) {
        Pattern pattern(1920, 480);
        Trigger chord;
        chord.start = 0;
        chord.duration = 120;
        chord.musical_data = Chord{60, {0, 4, 7}, inversion, 0, {}};
        (void)pattern.add(chord);
        std::vector<int> keys;
        for (const auto& note : scheduler.render_loop(pattern, 1, 0)) keys.push_back(note.key);
        std::sort(keys.begin(), keys.end());
        return keys;
    };
    require(voiced(0) == std::vector<int>{60, 64, 67}, "root position");
    require(voiced(1) == std::vector<int>{64, 67, 72}, "first inversion lifts the root");
    require(voiced(-1) == std::vector<int>{55, 60, 64},
            "a negative inversion drops the fifth below the root");
    require(voiced(4) == std::vector<int>{76, 79, 84},
            "inversion four is the first inversion an octave up");
}

// ---- Takes -----------------------------------------------------------------------

// Scenario: An imported pitch struck again inside one step becomes a ratchet
void roll_becomes_ratchet() {
    Pattern pattern(1920, 480);
    PlayedNote first;
    first.start = 0;
    first.duration = 30;
    first.key = 42;
    first.velocity = 0.8F;
    PlayedNote second = first;
    second.start = 50;
    require(write_played(pattern, first, 120, true) == 0, "the first hit lands on step 0");
    require(write_played(pattern, second, 120, true) == 0, "the second hit lands on step 0 too");
    // A live take keeps the rule that a pitch already on the step is not
    // written twice: only an imported file's roll grows a ratchet.
    Pattern live(1920, 480);
    (void)write_played(live, first, 120);
    (void)write_played(live, second, 120);
    require(live.events()[0].ratchets == 1, "a live re-strike leaves the step alone");
    require(pattern.events().size() == 1, "one step, not two triggers");
    require(pattern.events()[0].ratchets == 2, "the step plays the two hits as a ratchet");
    require(std::holds_alternative<Note>(pattern.events()[0].musical_data),
            "a repeated pitch is not a chord of one pitch");
}

// Scenario: A release ends the most recent press of its key
void release_pairs_newest_press() {
    TakeRecorder recorder(1920);
    recorder.note_on(0, 60, 1.0F, 0.0);
    recorder.note_on(100, 60, 1.0F, 0.0);
    const auto newest = recorder.note_off(150, 60);
    require(newest && newest->start == 100 && newest->duration == 50,
            "the release ends the press just before it");
    const auto oldest = recorder.note_off(300, 60);
    require(oldest && oldest->start == 0 && oldest->duration == 300,
            "the earlier press keeps its own release");
}

// ---- The engine ----------------------------------------------------------------

// Scenario: An edit that lengthens the song leaves the playhead where it was
void playhead_keeps_place() {
    Pattern pattern(1920, 480);
    (void)pattern.add(note_at(0, 120));
    const auto one_bar = song_of(pattern, {{0, 0, 0, 1}});
    SongEngine engine;
    std::string error;
    require(engine.prepare(one_bar, rate, block, 0, &error), "prepare: " + error);
    engine.set_playing(true);
    Pump pump(engine);
    // Two and a half laps of a one-bar song: half a bar in.
    (void)pump.left(bar_samples * 2 + bar_samples / 2);
    require(engine.sample_position() % engine.song_samples() == bar_samples / 2,
            "the playhead is half a bar in");
    const auto three_bars = song_of(pattern, {{0, 0, 0, 3}});
    require(engine.recompile(three_bars, 0, &error), "recompile: " + error);
    (void)pump.left(block);
    const auto position = engine.sample_position() % engine.song_samples();
    require(position == bar_samples / 2 + block,
            "the playhead is still half a bar in, not two and a half: " +
                std::to_string(position));
}

// Scenario: A fader pulled while the song plays ramps rather than steps
void fader_move_ramps() {
    Pattern pattern(1920, 480);
    (void)pattern.add(note_at(0, 1920));
    auto song = song_of(std::move(pattern));
    SongEngine engine;
    engine.set_instrument(0, clap_fixture());
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    engine.set_playing(true);
    Pump pump(engine);
    const auto before = pump.left(block);
    require(before.back() > 0.1F, "the note sounds");
    // Muted from the interface: the next block takes the gain to nothing.
    song.tracks[0].mix.mute = true;
    engine.apply_mix(song);
    const auto during = pump.left(block);
    require(std::abs(during.back()) < before.back() * 0.01F, "the block ends all but silent");
    require(during.front() > before.back() * 0.9F, "the block begins where the last one ended");
    float largest_step = 0.0F;
    for (std::size_t frame = 1; frame < during.size(); ++frame)
        largest_step = std::max(largest_step, std::abs(during[frame] - during[frame - 1]));
    require(largest_step < before.back() * 0.03F,
            "the gain falls by a small step per sample, never at once: " +
                std::to_string(largest_step));
    require(std::abs(during[SongEngine::smoothing_frames + 8]) < before.back() * 0.01F,
            "the ramp is over within the smoothing window");
    const auto after = pump.left(block);
    require(after.front() == 0.0F && after.back() == 0.0F, "the mute holds");
}

// Scenario: A bounce's tail holds the end of the song, not its start
void bounce_tail_holds_end() {
    Pattern pattern(1920, 480);
    (void)pattern.add(note_at(1800, 120));
    auto song = song_of(std::move(pattern));
    // An echo that outlives the song, under a strip lane that opens from
    // silence at bar 1 to unity by the song's end.
    song.tracks[0].inserts = {{{std::string(builtin_effect_format), "", "delay", {}}, false, {}}};
    AutomationTarget target;
    target.kind = AutomationTarget::Kind::gain;
    target.processor = track_instrument(0);
    song.tracks[0].automation.push_back({target, {{0, -96.0}, {1920, 0.0}}});
    SongEngine engine;
    engine.set_instrument(0, clap_fixture());
    engine.set_processor({BusKind::track, 0, 0}, create_builtin_effect("delay"));
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    require(engine.effect_tail_samples() > 0, "the delay reports a tail");
    const auto file = std::filesystem::path(BLOKKILY_TEST_ARTIFACTS) / "fidelity-tail.wav";
    const auto report = bounce_song(engine, file, WaveFormat::float32, 48000, &error);
    require(report.has_value(), "bounce: " + error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value(), "read back: " + error);
    require(wave->frames > engine.song_samples(), "the file has a tail");
    float tail_peak = 0.0F;
    for (std::uint64_t frame = engine.song_samples(); frame < wave->frames; ++frame)
        tail_peak = std::max(tail_peak, std::abs(wave->interleaved[static_cast<std::size_t>(
                                                     frame * wave->channels)]));
    require(tail_peak > 0.01F,
            "the echo in the tail is heard at the lane's end value, not faded to the lane's start");
}

// ---- Files -------------------------------------------------------------------------

// Scenario: A note played ahead of the first downbeat is exported
void pickup_note_exported() {
    Pattern pattern(1920, 480);
    auto pickup = note_at(0, 120);
    pickup.micro_offset = -10;
    (void)pattern.add(pickup);
    (void)pattern.add(note_at(480, 120, 62));
    const auto song = song_of(std::move(pattern));
    std::string error;
    const auto file = decode_midi_file(encode_midi_file(song), &error);
    require(file.has_value(), "decode: " + error);
    std::size_t notes = 0;
    bool pickup_found = false;
    for (const auto& track : file->tracks)
        for (const auto& note : track.notes) {
            ++notes;
            if (note.key == 60) pickup_found = note.start == 0 && note.duration == 110;
        }
    require(notes == 2, "both notes are written");
    require(pickup_found, "the pickup starts on the downbeat and keeps the rest of its length");
}

// Scenario: A project's numbers read the same whatever the machine's decimal mark
void project_numbers_locale_free() {
    Project project;
    project.song.tempo.points = {{0, 120.5, false}};
    project.song.tracks = {Track{}};
    project.song.tracks[0].mix.gain_db = -3.1;
    project.song.tracks[0].mix.pan = 0.25;
    const auto text = ProjectFile::serialize(project);
    require(text.find("120.5") != std::string::npos, "the tempo is written with a point");
    require(text.find("-3.1") != std::string::npos, "the gain is written with a point");
    require(text.find("0.25") != std::string::npos, "the pan is written with a point");
    const auto file = std::filesystem::path(BLOKKILY_TEST_ARTIFACTS) / "fidelity-locale.blok";
    std::string error;
    require(ProjectFile::save(project, file, &error), "save: " + error);
    const auto loaded = ProjectFile::load(file, &error);
    require(loaded.has_value(), "load: " + error);
    require(loaded->song.tempo.points[0].bpm == 120.5, "the tempo reads back exactly");
    require(loaded->song.tracks[0].mix.gain_db == -3.1, "the gain reads back exactly");
    require(loaded->song.tracks[0].mix.pan == 0.25, "the pan reads back exactly");
    // A number written with a comma, or not a number at all, is refused
    // rather than read as something else.
    auto broken = text;
    broken.replace(broken.find("120.5"), 5, "120,5");
    const auto rejected = std::filesystem::path(BLOKKILY_TEST_ARTIFACTS) / "fidelity-comma.blok";
    {
        std::ofstream out(rejected);
        out << broken;
    }
    require(!ProjectFile::load(rejected, &error).has_value(), "a comma decimal is refused");
    auto nan = text;
    nan.replace(nan.find("-3.1"), 4, "nan");
    {
        std::ofstream out(rejected);
        out << nan;
    }
    require(!ProjectFile::load(rejected, &error).has_value(), "a NaN is refused");
}

} // namespace

int main(int argc, char** argv) {
    const std::map<std::string, std::function<void()>> cases{
        {"overlap_hands_over", overlap_hands_over},
        {"loop_numbers_continue", loop_numbers_continue},
        {"inversion_octaves", inversion_octaves},
        {"roll_becomes_ratchet", roll_becomes_ratchet},
        {"release_pairs_newest_press", release_pairs_newest_press},
        {"playhead_keeps_place", playhead_keeps_place},
        {"fader_move_ramps", fader_move_ramps},
        {"bounce_tail_holds_end", bounce_tail_holds_end},
        {"pickup_note_exported", pickup_note_exported},
        {"project_numbers_locale_free", project_numbers_locale_free},
    };
    if (argc != 2 || !cases.contains(argv[1])) {
        std::cerr << "usage: blokkily_sequencer_fidelity_tests <case>\n";
        for (const auto& [name, run] : cases) std::cerr << "  " << name << '\n';
        return 2;
    }
    try {
        cases.at(argv[1])();
    } catch (const std::exception& failure) {
        std::cerr << "FIDELITY FAIL " << argv[1] << ": " << failure.what() << '\n';
        return 1;
    }
    std::cout << "FIDELITY PASS " << argv[1] << '\n';
    return 0;
}
