// Executable scenarios for features/chord_velocity.feature (plan item 1.6, the
// note-merging fix). Each case is its own CTest test, chord_velocity_<case>,
// and names the scenario it proves. Audio claims are read off audio rendered
// through the production callback (the deterministic pump), by the real CLAP
// fixture and by FluidSynth playing a real SoundFont.

#include "support/audio_probe.hpp"

#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/instruments/soundfont_synth.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/project/project.hpp"
#include "blokkily/sequencer/scheduler.hpp"
#include "blokkily/sequencer/take.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

bool near(double value, double expected, double tolerance = 1e-5) {
    return std::abs(value - expected) <= tolerance;
}

std::string describe(const std::vector<float>& values) {
    std::ostringstream out;
    out << '{';
    for (std::size_t index = 0; index < values.size(); ++index)
        out << (index ? ", " : "") << values[index];
    out << '}';
    return out.str();
}

std::string describe(const std::vector<Tick>& values) {
    std::ostringstream out;
    out << '{';
    for (std::size_t index = 0; index < values.size(); ++index)
        out << (index ? ", " : "") << values[index];
    out << '}';
    return out.str();
}

// Every voice of a chord as the model reads it: velocity and length, in
// interval order.
std::vector<float> velocities_of(const Chord& chord) {
    std::vector<float> result;
    for (std::size_t voice = 0; voice < chord.intervals.size(); ++voice)
        result.push_back(voice_velocity(chord, voice));
    return result;
}
std::vector<Tick> lengths_of(const Chord& chord, Tick trigger_duration) {
    std::vector<Tick> result;
    for (std::size_t voice = 0; voice < chord.intervals.size(); ++voice)
        result.push_back(voice_duration(chord, voice, trigger_duration));
    return result;
}

// Scenario: Keys merged onto one step keep how hard and how long each was
// played. The regression: write_played used to drop velocity (the merged
// chord played every voice at 0.8) and gave every voice the longest length.
void write_played_case() {
    Pattern pattern{1920, 480};
    require(write_played(pattern, {485, 56, 62, 0.6F, 0.0}, 120) == 4, "first key on step 4");
    require(write_played(pattern, {470, 300, 66, 0.9F, 50.0}, 120) == 4, "second key on step 4");
    require(pattern.events().size() == 1, "the two keys are one step");
    const auto& merged = pattern.events().front();
    const auto* chord = std::get_if<Chord>(&merged.musical_data);
    require(chord != nullptr, "the step became a chord");
    require(chord->intervals == std::vector<std::int16_t>({0, 4}) &&
                chord->cents == std::vector<double>({0.0, 50.0}),
            "the chord holds both pitches");
    // Both are checked before either fails, so the regression names every
    // part of the merge that was lost.
    const auto velocities = velocities_of(*chord);
    const auto lengths = lengths_of(*chord, merged.duration);
    const bool kept_velocity = velocities == std::vector<float>({0.6F, 0.9F});
    const bool kept_length = lengths == std::vector<Tick>({56, 300});
    require(kept_velocity && kept_length,
            std::string("REGRESSION:") +
                (kept_velocity ? "" : " merged voices must keep their own velocity {0.6, 0.9}, got " +
                                          describe(velocities) + ";") +
                (kept_length ? "" : " merged voices must keep their own length {56, 300}, got " +
                                        describe(lengths)));
    require(merged.duration == 300 && merged.micro_offset == 5,
            "the step lasts as long as its longest voice and keeps its timing");

    // A third key lands among them, in pitch order, keeping its own too.
    require(write_played(pattern, {480, 120, 64, 0.3F, 0.0}, 120) == 4, "third key on step 4");
    const auto& three = std::get<Chord>(pattern.events().front().musical_data);
    require(three.intervals == std::vector<std::int16_t>({0, 2, 4}), "three voices in order");
    require(velocities_of(three) == std::vector<float>({0.6F, 0.3F, 0.9F}),
            "the third voice keeps its velocity, got " + describe(velocities_of(three)));
    require(lengths_of(three, pattern.events().front().duration) == std::vector<Tick>({56, 120, 300}),
            "the third voice keeps its length");
    // A pitch the step already sounds is not written twice, and nothing moves.
    require(write_played(pattern, {480, 900, 66, 0.1F, 50.0}, 120) == 4, "repeat pitch");
    require(velocities_of(std::get<Chord>(pattern.events().front().musical_data)) ==
                std::vector<float>({0.6F, 0.3F, 0.9F}),
            "a repeated pitch changes nothing");

    // A chord struck as one (a chord pad, at one velocity) gains a played
    // voice without its own voices changing.
    Pattern padded{1920, 480};
    Trigger pad;
    pad.start = 240;
    pad.duration = 200;
    Chord struck{60, {0, 7}, 0, 0, {}};
    struck.velocity = 0.5F;
    pad.musical_data = struck;
    (void)padded.add(pad);
    require(write_played(padded, {240, 100, 64, 0.9F, 0.0}, 120) == 2, "key onto the pad step");
    const auto& grown = padded.events().front();
    const auto& grown_chord = std::get<Chord>(grown.musical_data);
    require(velocities_of(grown_chord) == std::vector<float>({0.5F, 0.9F, 0.5F}),
            "the pad's voices keep the pad's velocity, got " + describe(velocities_of(grown_chord)));
    require(lengths_of(grown_chord, grown.duration) == std::vector<Tick>({200, 100, 200}),
            "the pad's voices keep the pad's length, got " +
                describe(lengths_of(grown_chord, grown.duration)));

    // Keys struck alike and held alike stay a plain chord: nothing per voice.
    Pattern alike{1920, 480};
    (void)write_played(alike, {0, 60, 60, 0.7F, 0.0}, 120);
    (void)write_played(alike, {0, 60, 67, 0.7F, 0.0}, 120);
    const auto& plain = std::get<Chord>(alike.events().front().musical_data);
    require(plain.velocities.empty() && plain.durations.empty() && plain.velocity == 0.7F,
            "alike voices are stored once, as the chord's own");

    // The scheduler plays what was kept.
    const auto notes = Scheduler{}.render(pattern, 1, 0).notes;
    require(notes.size() == 3, "three voices are scheduled");
    for (const auto& note : notes) {
        require(note.start == 485, "every voice starts where the step was played");
        if (note.key == 62) require(note.velocity == 0.6F && note.duration == 56, "voice 62");
        else if (note.key == 64) require(note.velocity == 0.3F && note.duration == 120, "voice 64");
        else if (note.key == 66) require(note.velocity == 0.9F && note.duration == 300, "voice 66");
        else require(false, "an unexpected voice was scheduled");
    }

    // The model refuses a chord whose per-voice lists do not match its voices.
    Pattern refused{1920, 480};
    Trigger bad;
    Chord mismatched{60, {0, 4, 7}, 0, 0, {}};
    mismatched.velocities = {0.5F};
    bad.musical_data = mismatched;
    bool threw = false;
    try { (void)refused.add(bad); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "one velocity for three voices is refused");
    mismatched.velocities.clear();
    mismatched.durations = {10, 0, 10};
    bad.musical_data = mismatched;
    threw = false;
    try { (void)refused.add(bad); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "a zero-length voice is refused");
}

// Drives the production render callback a block at a time, so the song can be
// edited between blocks the way the interface edits it while it plays.
class Pump {
public:
    explicit Pump(AudioSource& source) : output_(RtAudioOutput::Mode::deterministic) {
        require(output_.open(source) && output_.start(), "the production output must start");
    }
    // The left channel of the next `frames` frames.
    std::vector<float> left(std::size_t frames) {
        std::vector<float> stereo(frames * 2, -1.0F);
        require(output_.pump(stereo), "the production callback must render");
        stereo.resize(frames);
        return stereo;
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

// One track, hard left so the left channel is the track at unity, playing one
// pattern once per loop.
Song song_of(Pattern pattern) {
    Song song;
    song.patterns = {{"Chords", std::move(pattern)}};
    song.tracks = {Track{}};
    song.tracks[0].mix.pan = -1.0;
    song.clips = {{0, 0, 0, 1}};
    return song;
}

// The fixture in velocity mode sounds level (0.25) × the sum of the held
// velocities. A lock on the step turns the mode on through the real
// parameter-event path; it lands with the notes, before anything is heard.
constexpr float fixture_level = 0.25F;
ParameterLock velocity_mode_lock() {
    return {"velocity-mode", 2, 1.0, ParameterLock::Kind::automation};
}

// The level of `audio` over [from, to), which must be flat: a plateau.
float plateau(const std::vector<float>& audio, std::size_t from, std::size_t to,
              const std::string& what) {
    const float level = audio.at(from);
    for (std::size_t sample = from; sample < to; ++sample)
        require(near(audio.at(sample), level), what + ": not flat at sample " +
                                                   std::to_string(sample) + " (" +
                                                   std::to_string(audio[sample]) + " vs " +
                                                   std::to_string(level) + ")");
    return level;
}

// Scenario: Each voice sounds as hard and as long as it was played, and
// Scenario: Editing one voice changes only that voice.
void scheduler_clap_case() {
    // One tick per sample: 24000 ticks a beat at 120 BPM and 48 kHz.
    Pattern pattern(2048, 24000);
    Trigger step;
    step.start = 100;
    step.duration = 800;
    Chord chord{60, {0, 4, 7}, 0, 0, {}};
    chord.velocities = {0.6F, 0.9F, 0.2F};
    chord.durations = {800, 400, 200};
    step.musical_data = chord;
    step.locks = {velocity_mode_lock()};
    const auto id = pattern.add(step);
    auto song = song_of(std::move(pattern));

    SongEngine engine;
    engine.set_instrument(0, clap_fixture());
    std::string error;
    require(engine.prepare(song, 120.0, 48000.0, 256, 0, &error), "prepare: " + error);
    require(engine.song_samples() == 2048, "one tick per sample");
    engine.set_playing(true);
    Pump pump(engine);

    const auto first = pump.left(2048);
    require(plateau(first, 0, 100, "before the step") == 0.0F, "silent before the step");
    const float all = plateau(first, 100, 300, "all three voices");
    require(near(all, fixture_level * 1.7F),
            "three voices at 0.6, 0.9 and 0.2 must sound 0.425, heard " + std::to_string(all));
    const float two = plateau(first, 300, 500, "after the shortest voice");
    require(near(two, fixture_level * 1.5F),
            "the 200-tick voice must end on its own, leaving 0.375, heard " + std::to_string(two));
    const float one = plateau(first, 500, 900, "the longest voice alone");
    require(near(one, fixture_level * 0.6F),
            "the 400-tick voice must end on its own, leaving 0.15, heard " + std::to_string(one));
    require(plateau(first, 900, 2048, "after the step") == 0.0F, "silent after the longest voice");

    // One voice is edited while the song plays: the next loop hears it.
    auto& edited = song.patterns[0].pattern;
    auto replacement = *edited.find(id);
    std::get<Chord>(replacement.musical_data).velocities[2] = 0.8F;
    require(edited.update(id, replacement), "edit one voice");
    require(engine.recompile(song, 120.0, 0, &error), "recompile: " + error);
    const auto second = pump.left(2048);
    const float louder = plateau(second, 100, 300, "all three voices after the edit");
    require(near(louder, fixture_level * 2.3F),
            "raising one voice to 0.8 must sound 0.575, heard " + std::to_string(louder));
    require(near(plateau(second, 300, 500, "two voices after the edit"), fixture_level * 1.5F),
            "the voices that were not edited are unchanged");
    require(near(plateau(second, 500, 900, "one voice after the edit"), fixture_level * 0.6F),
            "the longest voice is unchanged");
    std::cout << "plateaus: " << all << ' ' << two << ' ' << one << " then " << louder << '\n';
}

// A format 4 file, written before chords had velocities: its chord must play
// every voice at 0.8 for the trigger's length.
const std::string v4_chord_project =
    "blokkily-project 4\n"
    "name Old\n"
    "tempo 120\n"
    "master 0\n"
    "tuning 12-TET 1200 69 12 0 100 200 300 400 500 600 700 800 900 1000 1100\n"
    "scale Chromatic 12 0 100 200 300 400 500 600 700 800 900 1000 1100\n"
    "harmony 60 0\n"
    "pattern Old 2048 24000\n"
    "trigger 0 1 100 600 0 1 1 0 chord 60 0 0 3 0 4 7 0 0 0\n"
    "lock 0 1 velocity-mode 2 automation 1\n"
    "track Lead 0 -1 0 0 ~ ~ ~ ~\n"
    "clip 0 0 0 1\n";

// Scenario: A chord saved before chord velocity plays as it always did.
void legacy_chord_case() {
    std::string error;
    const auto project = ProjectFile::parse(v4_chord_project, &error);
    require(project.has_value(), "a v4 chord parses: " + error);
    const auto& chord = std::get<Chord>(project->song.patterns[0].pattern.events().front().musical_data);
    require(chord.velocity == 0.8F && chord.velocities.empty() && chord.durations.empty(),
            "a v4 chord has the default velocity and nothing per voice");
    SongEngine engine;
    engine.set_instrument(0, clap_fixture());
    require(engine.prepare(project->song, 120.0, 48000.0, 256, 0, &error), "prepare: " + error);
    engine.set_playing(true);
    Pump pump(engine);
    const auto audio = pump.left(2048);
    const float level = plateau(audio, 100, 700, "the v4 chord");
    require(near(level, fixture_level * 3 * 0.8F),
            "three voices at 0.8 must sound 0.6, heard " + std::to_string(level));
    require(plateau(audio, 700, 2048, "after the v4 chord") == 0.0F,
            "every voice ends with the trigger");
}

// Scenario: A SoundFont hears each voice's velocity.
void soundfont_case() {
#ifndef BLOKKILY_TEST_SF2_PATH
    std::cout << "no test SoundFont configured\n";
    std::exit(77);
#else
    // Renders a C4 + G4 chord through FluidSynth with the given velocities and
    // returns the Goertzel energy at C4 over the energy at G4.
    const auto ratio = [](float c4, float g4) {
        Pattern pattern{1920, 480};
        Trigger step;
        step.start = 0;
        step.duration = 960;
        Chord chord{60, {0, 7}, 0, 0, {}};
        chord.velocities = {c4, g4};
        step.musical_data = chord;
        (void)pattern.add(step);
        auto song = song_of(std::move(pattern));
        auto synth = std::make_unique<SoundFontSynth>();
        require(synth->load(BLOKKILY_TEST_SF2_PATH), "the SoundFont must load");
        SongEngine engine;
        engine.set_instrument(0, std::move(synth));
        std::string error;
        require(engine.prepare(song, 120.0, 48000.0, 512, 0, &error), "prepare: " + error);
        engine.set_playing(true);
        Pump pump(engine);
        const auto audio = pump.left(24000);
        const std::span<const float> held{audio.data() + 4800, 19200};
        require(probe::peak(held) > 1e-3F, "the SoundFont chord must not be silent");
        const double c = probe::goertzel_energy(held, 48000.0, 261.6256);
        const double g = probe::goertzel_energy(held, 48000.0, 391.9954);
        require(c > 0.0 && g > 0.0, "both voices must be heard");
        return c / g;
    };
    const double c_loud = ratio(1.0F, 0.2F);
    const double g_loud = ratio(0.2F, 1.0F);
    std::cout << "C4/G4 energy: " << c_loud << " with C4 loud, " << g_loud << " with G4 loud\n";
    require(c_loud / g_loud > 4.0,
            "swapping the voices' velocities must flip the C4/G4 balance by more than 4x, got " +
                std::to_string(c_loud / g_loud));
#endif
}

// Scenario: Chord velocities and lengths survive a save and a load.
void project_case() {
    Project project;
    project.name = "Voices";
    Pattern pattern{1920, 480};
    Trigger merged;
    merged.start = 480;
    merged.duration = 300;
    Chord voiced{62, {0, 4}, 0, 0, {0.0, 50.0}};
    voiced.velocities = {0.6F, 0.9F};
    voiced.durations = {56, 300};
    merged.musical_data = voiced;
    const auto merged_id = pattern.add(merged);
    Trigger padded;
    padded.start = 960;
    Chord pad{60, {0, 4, 7}, 0, 0, {}};
    pad.velocity = 0.9F;
    padded.musical_data = pad;
    const auto pad_id = pattern.add(padded);
    Trigger plain;
    plain.start = 1440;
    plain.musical_data = Chord{60, {0, 3, 7}, 0, 0, {}};
    const auto plain_id = pattern.add(plain);
    project.song.patterns = {{"Voices", std::move(pattern)}};

    const auto text = ProjectFile::serialize(project);
    const auto has_line = [&text](const std::string& line) {
        return text.find('\n' + line + '\n') != std::string::npos;
    };
    require(has_line("voicevel 0 " + std::to_string(merged_id) + " 0.800000012 2 0.600000024 0.899999976"),
            "the merged chord's velocities are written:\n" + text);
    require(has_line("voicelen 0 " + std::to_string(merged_id) + " 2 56 300"),
            "the merged chord's lengths are written:\n" + text);
    require(has_line("voicevel 0 " + std::to_string(pad_id) + " 0.899999976 0"),
            "the pad's chord velocity is written:\n" + text);
    require(text.find("voicevel 0 " + std::to_string(plain_id) + ' ') == std::string::npos &&
                text.find("voicelen 0 " + std::to_string(pad_id) + ' ') == std::string::npos &&
                text.find("voicelen 0 " + std::to_string(plain_id) + ' ') == std::string::npos,
            "a default chord writes no voice records");
    // The chord line itself keeps its format 4 shape.
    require(has_line("trigger 0 " + std::to_string(merged_id) + " 480 300 0 1 1 0 chord 62 0 0 2 0 4 0 50"),
            "the chord line is unchanged:\n" + text);

    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value(), "the project reloads: " + error);
    const auto& events = loaded->song.patterns[0].pattern;
    const auto& back = std::get<Chord>(events.find(merged_id)->musical_data);
    require(back.velocities == voiced.velocities && back.durations == voiced.durations &&
                back.velocity == 0.8F,
            "the merged chord reloads with its voices");
    const auto& pad_back = std::get<Chord>(events.find(pad_id)->musical_data);
    require(pad_back.velocity == 0.9F && pad_back.velocities.empty(), "the pad reloads");
    const auto& plain_back = std::get<Chord>(events.find(plain_id)->musical_data);
    require(plain_back.velocity == 0.8F && plain_back.velocities.empty() &&
                plain_back.durations.empty(),
            "the plain chord reloads as a plain chord");
    require(ProjectFile::serialize(*loaded) == text, "a reloaded project saves byte-identically");

    // Malformed voice records fail loudly and say why.
    const auto refuses = [&text](const std::string& extra, const std::string& reason) {
        std::string why;
        const auto parsed = ProjectFile::parse(text + extra + '\n', &why);
        require(!parsed.has_value() && !why.empty(), "refused: " + reason);
    };
    const auto id = std::to_string(merged_id);
    refuses("voicevel 0 " + id + " 0.8 3 0.1 0.2 0.3", "three velocities for two voices");
    refuses("voicevel 0 " + id + " 1.5 0", "a chord velocity above 1");
    refuses("voicevel 0 " + id + " 0.8 2 0.1 -0.2", "a negative voice velocity");
    refuses("voicevel 0 " + id + " 0.8 2 0.1", "a short velocity record");
    refuses("voicevel 0 999 0.8 0", "an unknown trigger");
    refuses("voicevel 7 " + id + " 0.8 0", "an unknown pattern");
    refuses("voicelen 0 " + id + " 2 56 0", "a zero-length voice");
    refuses("voicelen 0 " + id + " 1 56", "one length for two voices");
    refuses("voicelen 0 " + id + " 0", "an empty length record");
    // A note is not a chord: it has one velocity already.
    Project noted;
    Trigger note;
    note.musical_data = Note{};
    const auto note_id = noted.song.patterns[0].pattern.add(note);
    std::string why;
    require(!ProjectFile::parse(ProjectFile::serialize(noted) + "voicevel 0 " +
                                    std::to_string(note_id) + " 0.5 0\n",
                                &why)
                 .has_value(),
            "voice records on a note are refused");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "pass a case name");
        const std::string name = argv[1];
        if (name == "write_played") write_played_case();
        else if (name == "scheduler_clap") scheduler_clap_case();
        else if (name == "legacy_chord") legacy_chord_case();
        else if (name == "soundfont") soundfont_case();
        else if (name == "project") project_case();
        else throw std::runtime_error("unknown case " + name);
        std::cout << name << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
