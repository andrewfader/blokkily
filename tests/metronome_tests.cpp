// The metronome and count-in (item 3.7, decision 14), proved from what the
// production SongEngine::process() rendered, or wrote to a file and read back,
// with the real CLAP fixture as the song's instrument. Expected click
// positions are worked out here in closed form from the tempo and the meter,
// not by asking the clock under test.
//
// Run with a case name; each case is its own CTest test (metronome_<case>).
// features/metronome.feature names the scenario each case executes.

#include "support/audio_probe.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/project/project.hpp"
#include "blokkily/sequencer/take.hpp"

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <span>
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
// Samples one click lasts (30 ms at 48 kHz).
constexpr std::size_t click_frames = 1440;

std::unique_ptr<PluginInstance> clap() {
    std::string error;
    auto instance = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
    require(instance != nullptr, "the CLAP fixture must load: " + error);
    return instance;
}

Trigger note(Tick start, Tick duration, int key = 60) {
    Trigger trigger;
    trigger.start = start;
    trigger.duration = duration;
    trigger.musical_data = Note{static_cast<std::int16_t>(key), 1.0F, 0.0F};
    return trigger;
}

// A song of one pattern `length` ticks long on one track, placed once.
Song song_of(Tick length, const std::vector<Trigger>& triggers = {}, std::size_t tracks = 1) {
    Pattern pattern(length, 480);
    for (const auto& trigger : triggers) (void)pattern.add(trigger);
    Song song;
    song.patterns = {{"P", std::move(pattern)}};
    song.tracks.assign(tracks, Track{});
    song.clips.clear();
    for (std::size_t track = 0; track < tracks; ++track) song.clips.push_back({track, 0, 0, 1});
    return song;
}

struct Stereo {
    std::vector<float> left;
    std::vector<float> right;
};

// `frames` frames of the production callback, in blocks of `block`.
Stereo render(SongEngine& engine, std::size_t frames) {
    Stereo out{std::vector<float>(frames, 0.0F), std::vector<float>(frames, 0.0F)};
    for (std::size_t at = 0; at < frames; at += block) {
        const auto count = std::min<std::size_t>(block, frames - at);
        engine.process({std::span{out.left}.subspan(at, count),
                        std::span{out.right}.subspan(at, count)});
    }
    return out;
}

// Where a click starts: a sample away from silence after at least `quiet`
// silent samples. A click is a cosine burst that starts at its loudest, so
// its first sample is the sample its beat falls on.
std::vector<std::size_t> onsets(std::span<const float> samples, std::size_t quiet = 64) {
    std::vector<std::size_t> found;
    std::size_t silent = quiet;
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (std::abs(samples[index]) > 1e-6F) {
            if (silent >= quiet) found.push_back(index);
            silent = 0;
        } else {
            ++silent;
        }
    }
    return found;
}

std::string list(const std::vector<std::size_t>& values) {
    std::string text = "{";
    for (std::size_t index = 0; index < values.size(); ++index)
        text += (index ? ", " : "") + std::to_string(values[index]);
    return text + "}";
}

// Every onset within one sample of the one expected, and no more or fewer.
void require_onsets(const std::vector<std::size_t>& found, const std::vector<std::size_t>& wanted,
                    const std::string& what) {
    bool same = found.size() == wanted.size();
    for (std::size_t index = 0; same && index < found.size(); ++index) {
        const auto distance = static_cast<long long>(found[index]) -
                              static_cast<long long>(wanted[index]);
        same = distance >= -1 && distance <= 1;
    }
    require(same, what + ": clicks at " + list(found) + ", wanted " + list(wanted));
}

float click_peak(std::span<const float> samples, std::size_t at) {
    const auto count = std::min(click_frames, samples.size() - at);
    return probe::peak(samples.subspan(at, count));
}

// Accented clicks at `accents` (indices into `found`) are louder than every
// other click.
void require_accents(std::span<const float> samples, const std::vector<std::size_t>& found,
                     const std::vector<std::size_t>& accents, const std::string& what) {
    float quietest_accent = 1e9F;
    float loudest_beat = 0.0F;
    for (std::size_t index = 0; index < found.size(); ++index) {
        const float peak = click_peak(samples, found[index]);
        if (std::find(accents.begin(), accents.end(), index) != accents.end())
            quietest_accent = std::min(quietest_accent, peak);
        else
            loudest_beat = std::max(loudest_beat, peak);
    }
    require(quietest_accent > 1.5F * loudest_beat,
            what + ": downbeats peak at " + std::to_string(quietest_accent) +
                ", other beats at " + std::to_string(loudest_beat));
}

std::filesystem::path scratch_file(const std::string& name) {
    return std::filesystem::temp_directory_path() /
           ("blokkily-metronome-" + std::to_string(::getpid()) + "-" + name);
}

// --------------------------------------------------------------- the click

// features/metronome.feature: The click follows a tempo step.
void clicks_on_tempo_step_case() {
    // Four bars of 4/4: two at 120 BPM, then 90 BPM from bar 3 (tick 3840).
    auto song = song_of(7680);
    song.tempo.points = {TempoPoint{0, 120.0, false}, TempoPoint{3840, 90.0, false}};
    song.metronome = {true, 0.0, 0};
    SongEngine engine;
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    // 120 BPM: 24000 samples a quarter; 90 BPM: 32000, from sample 192000.
    std::vector<std::size_t> wanted;
    for (std::size_t beat = 0; beat < 8; ++beat) wanted.push_back(beat * 24000);
    for (std::size_t beat = 0; beat < 8; ++beat) wanted.push_back(192000 + beat * 32000);
    require(engine.song_samples() == 448000, "the song lasts 448000 samples");
    // The same beats, placed by the clock the engine plays: one sample rule.
    const TickClock clock(song.tempo, 480, rate);
    for (std::size_t beat = 0; beat < 16; ++beat) {
        const auto placed = static_cast<long long>(
            sample_for_tick(clock, static_cast<double>(beat * 480)));
        require(std::abs(placed - static_cast<long long>(wanted[beat])) <= 1,
                "the tick clock puts beat " + std::to_string(beat) + " where the song has it");
    }
    engine.set_playing(true);
    const auto out = render(engine, engine.song_samples());
    const auto found = onsets(out.left);
    require_onsets(found, wanted, "a tempo step");
    require_accents(out.left, found, {0, 4, 8, 12}, "a tempo step");
    // Both channels carry the click, at 0 dB a downbeat at full scale.
    require(out.left == out.right, "the click is the same in both channels");
    require(std::abs(out.left[0] - 1.0F) < 1e-6F, "a 0 dB downbeat starts at full scale");
}

// features/metronome.feature: The click follows a 7/8 meter.
void clicks_in_seven_eight_case() {
    // A bar of 4/4, then two bars of 7/8 (1680 ticks each), at 120 BPM.
    auto song = song_of(1920 + 2 * 1680);
    song.meter.changes = {MeterChange{0, 4, 4}, MeterChange{1, 7, 8}};
    song.metronome = {true, -6.0, 0};
    SongEngine engine;
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    std::vector<std::size_t> wanted;
    for (std::size_t beat = 0; beat < 4; ++beat) wanted.push_back(beat * 24000);
    // An eighth is 12000 samples: the 7/8 bars click on eighths.
    for (std::size_t beat = 0; beat < 14; ++beat) wanted.push_back(96000 + beat * 12000);
    engine.set_playing(true);
    const auto out = render(engine, engine.song_samples());
    const auto found = onsets(out.left);
    require_onsets(found, wanted, "a 7/8 meter");
    require_accents(out.left, found, {0, 4, 11}, "a 7/8 meter");
}

// features/metronome.feature: The click has its own level and switch.
void click_level_and_switch_case() {
    auto song = song_of(1920);
    song.metronome = {true, 0.0, 0};
    SongEngine engine;
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    engine.set_playing(true);
    const auto full = render(engine, engine.song_samples());
    // Live: the level moves without a prepare, and the next pass is quieter.
    song.metronome.level_db = -12.0;
    engine.apply_mix(song);
    const auto quieter = render(engine, engine.song_samples());
    const double ratio = click_peak(quieter.left, 0) / click_peak(full.left, 0);
    require(std::abs(ratio - std::pow(10.0, -12.0 / 20.0)) < 0.002,
            "-12 dB scales the click by 0.251, measured " + std::to_string(ratio));
    song.metronome.enabled = false;
    engine.apply_mix(song);
    require(!engine.metronome_enabled(), "the click is off");
    const auto off = render(engine, engine.song_samples());
    require(probe::peak(off.left) == 0.0F && probe::peak(off.right) == 0.0F,
            "a song with the click off and nothing else is silent");
    // Stopped, no click starts.
    song.metronome.enabled = true;
    engine.apply_mix(song);
    engine.set_playing(false);
    engine.seek(0);
    const auto stopped = render(engine, 48000);
    require(probe::peak(stopped.left) == 0.0F, "no click sounds on a stopped transport");
}

// features/metronome.feature: Solo, mute and the faders do not reach the click.
void click_ignores_solo_and_mute_case() {
    // Two CLAP tracks, hard left and hard right, playing off-beat notes that
    // never overlap a click: an eighth after each beat, 60 ticks long.
    std::vector<Trigger> offbeats;
    for (Tick beat = 0; beat < 4; ++beat) offbeats.push_back(note(beat * 480 + 240, 60));
    auto song = song_of(1920, offbeats, 2);
    song.tracks[0].mix.pan = -1.0;
    song.tracks[1].mix.pan = 1.0;
    song.metronome = {true, -6.0, 0};
    SongEngine engine;
    engine.set_instrument(0, clap());
    engine.set_instrument(1, clap());
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    engine.set_playing(true);
    const auto open = render(engine, engine.song_samples());
    const auto click_windows_equal = [](const Stereo& a, const Stereo& b) {
        for (std::size_t beat = 0; beat < 4; ++beat)
            for (std::size_t frame = beat * 24000; frame < beat * 24000 + click_frames; ++frame)
                if (a.left[frame] != b.left[frame] || a.right[frame] != b.right[frame])
                    return false;
        return true;
    };
    const auto note_window = [](const std::vector<float>& channel, std::size_t beat) {
        return std::span<const float>{channel}.subspan(beat * 24000 + 12000, 3000);
    };
    require(probe::peak(note_window(open.left, 0)) > 0.01F &&
                probe::peak(note_window(open.right, 0)) > 0.01F,
            "both tracks sound between the beats");
    require(click_peak(open.left, 0) > 0.4F, "the click sounds on the downbeat");

    // Every track muted: the song is gone from the bus, the click is not.
    song.tracks[0].mix.mute = true;
    song.tracks[1].mix.mute = true;
    engine.apply_mix(song);
    const auto muted = render(engine, engine.song_samples());
    require(probe::peak(note_window(muted.left, 1)) == 0.0F &&
                probe::peak(note_window(muted.right, 1)) == 0.0F,
            "muted tracks leave the bus");
    require(click_windows_equal(open, muted), "muting every track leaves the click as it was");

    // Track 1 soloed: the left track leaves the bus, the click does not.
    song.tracks[0].mix.mute = false;
    song.tracks[1].mix.mute = false;
    song.tracks[1].mix.solo = true;
    // The master fader is the song's, not the click's.
    song.master_gain_db = -24.0;
    engine.apply_mix(song);
    const auto soloed = render(engine, engine.song_samples());
    // Hard right leaves track 2 at the pan law's cos(pi/2) on the left.
    require(probe::peak(note_window(soloed.left, 2)) < 1e-6F &&
                probe::peak(note_window(soloed.right, 2)) > 0.0F,
            "a solo on track 2 leaves only track 2 on the bus");
    require(click_windows_equal(open, soloed),
            "a solo and the master fader leave the click as it was");
}

// ---------------------------------------------------------------- bounces

// features/metronome.feature: A bounce leaves the click out unless asked.
void bounce_click_option_case() {
    std::vector<Trigger> offbeats;
    for (Tick beat = 0; beat < 8; ++beat) offbeats.push_back(note(beat * 480 + 240, 60));
    auto song = song_of(3840, offbeats);
    song.metronome = {true, -6.0, 0};
    SongEngine engine;
    engine.set_instrument(0, clap());
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    const auto samples = engine.song_samples();
    require(samples == 192000, "two bars at 120 BPM");

    // What the engine plays live, from the top, with and without the click.
    const auto live = [&](bool click) {
        engine.set_metronome_enabled(click);
        engine.reset_processing();
        engine.seek(0);
        engine.set_playing(true);
        auto out = render(engine, samples);
        engine.set_playing(false);
        (void)render(engine, block);
        return out;
    };
    const auto read_back = [&](const std::filesystem::path& file) {
        const auto wave = read_wave(file, &error);
        require(wave.has_value(), "the bounce reads back: " + error);
        require(wave->channels == 2 && wave->frames >= samples, "a stereo file of the song");
        Stereo out{std::vector<float>(samples), std::vector<float>(samples)};
        for (std::uint64_t frame = 0; frame < samples; ++frame) {
            out.left[frame] = wave->interleaved[frame * 2];
            out.right[frame] = wave->interleaved[frame * 2 + 1];
        }
        return out;
    };
    const auto equal = [](const Stereo& a, const Stereo& b) {
        return a.left == b.left && a.right == b.right;
    };

    const auto plain_file = scratch_file("plain.wav");
    require(bounce_song(engine, plain_file, WaveFormat::float32, 0, &error).has_value(),
            "the default bounce: " + error);
    require(engine.metronome_enabled(), "the bounce put the session's click back on");
    const auto plain = read_back(plain_file);
    require(onsets(plain.left, 64).size() == 8, "the default bounce has the 8 notes and no click");
    require(probe::peak(std::span<const float>{plain.left}.subspan(0, click_frames)) == 0.0F,
            "the default bounce is silent where the downbeat's click would be");
    require(equal(plain, live(false)), "the default bounce is the engine's render without the click");

    const auto click_file = scratch_file("click.wav");
    require(bounce_song(engine, click_file, WaveFormat::float32, 0,
                        BounceOptions{true}, &error).has_value(),
            "the bounce with the click: " + error);
    const auto clicked = read_back(click_file);
    const auto with_click = live(true);
    require(equal(clicked, with_click), "the bounce with the click is the engine's live render");
    require(std::abs(clicked.left[0] - static_cast<float>(std::pow(10.0, -6.0 / 20.0))) < 1e-6F,
            "the downbeat's click opens the file at the session's level");
    require(onsets(clicked.left, 64).size() == 16, "8 clicks and 8 notes in the bounce");
    std::filesystem::remove(plain_file);
    std::filesystem::remove(click_file);
}

// ---------------------------------------------------------------- count-in

// features/metronome.feature: A count-in plays N bars before the song, which
// waits where it is.
void count_in_bars_case() {
    // Bars 1 and 2 of 4/4, then a bar of 7/8. A note on the downbeat of bar 2
    // and of bar 3 marks where the song is heard to start.
    auto song = song_of(1920 * 2 + 1680, {note(1920, 240), note(3840, 120)});
    song.meter.changes = {MeterChange{0, 4, 4}, MeterChange{2, 7, 8}};
    // The click is off: a count-in clicks whatever the click's switch says.
    song.metronome = {false, -6.0, 2};
    SongEngine engine;
    engine.set_instrument(0, clap());
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);

    // From bar 2 (sample 96000), two bars of count-in: 192000 samples.
    engine.seek(96000);
    engine.play_with_count_in(2);
    std::vector<float> left(192000 + 48000, 0.0F);
    std::vector<float> right(left.size(), 0.0F);
    bool held = true;
    bool counted = true;
    for (std::size_t at = 0; at < left.size(); at += block) {
        const auto count = std::min<std::size_t>(block, left.size() - at);
        engine.process({std::span{left}.subspan(at, count), std::span{right}.subspan(at, count)});
        if (at + count < 192000) {
            held = held && engine.sample_position() == 96000;
            counted = counted && engine.counting_in();
        }
    }
    require(held, "the playhead stays on bar 2 through the count-in");
    require(counted, "the engine reports the count-in while it plays");
    require(!engine.counting_in(), "the count-in is over once the song plays");
    require(engine.sample_position() == 96000 + 48000,
            "the song plays on from bar 2 after the count-in: at " +
                std::to_string(engine.sample_position()));
    const auto found = onsets(left);
    std::vector<std::size_t> wanted;
    for (std::size_t beat = 0; beat < 8; ++beat) wanted.push_back(beat * 24000);
    // Then the song's first sample: the note on bar 2's downbeat.
    wanted.push_back(192000);
    require_onsets(found, wanted, "two bars of count-in, then the song");
    std::vector<std::size_t> clicks(found.begin(), found.end() - 1);
    require_accents(left, clicks, {0, 4}, "the count-in");
    require(click_peak(left, 192000) > 0.0F &&
                std::abs(left[192000] - left[192000 + 2000]) < 1e-6F,
            "what starts at sample 192000 is the song's held note, not a click");

    // In a 7/8 bar a one-bar count-in is seven eighths: 84000 samples.
    engine.set_playing(false);
    (void)render(engine, 48000);
    engine.seek(192000);
    engine.play_with_count_in(1);
    std::vector<float> seven(84000 + 24000, 0.0F);
    std::vector<float> seven_right(seven.size(), 0.0F);
    held = true;
    for (std::size_t at = 0; at < seven.size(); at += block) {
        const auto count = std::min<std::size_t>(block, seven.size() - at);
        engine.process({std::span{seven}.subspan(at, count),
                        std::span{seven_right}.subspan(at, count)});
        if (at + count <= 84000) held = held && engine.sample_position() == 192000;
    }
    require(held, "the playhead stays on bar 3 through its count-in");
    wanted.clear();
    for (std::size_t beat = 0; beat < 7; ++beat) wanted.push_back(beat * 12000);
    wanted.push_back(84000);
    const auto seven_found = onsets(seven);
    require_onsets(seven_found, wanted, "a bar of 7/8 count-in, then the song");
    require_accents(seven, {seven_found.begin(), seven_found.end() - 1}, {0},
                    "the 7/8 count-in");
}

// features/metronome.feature: Notes played into the count-in.
void count_in_recording_case() {
    auto song = song_of(3840);
    song.metronome = {false, -6.0, 1};
    SongEngine engine;
    engine.set_instrument(0, clap());
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    InputQueue port;
    engine.connect_input(&port);
    engine.set_recording(true);
    engine.play_with_count_in(1);   // one bar: 96000 samples

    const auto key = [](PluginEvent::Type type, int pitch, double velocity) {
        return RoutedEvent{0, {type, 0, pitch, velocity, 0.0}};
    };
    std::vector<float> left(96000 + 48000 + 20 * block, 0.0F);
    std::vector<float> right(left.size(), 0.0F);
    std::size_t index = 0;
    for (std::size_t at = 0; at < left.size(); at += block, ++index) {
        // Played and let go inside the count-in: heard, not recorded.
        if (index == 20) require(port.push(key(PluginEvent::Type::note_on, 64, 0.8)), "push");
        if (index == 100) require(port.push(key(PluginEvent::Type::note_off, 64, 0.0)), "push");
        // Played inside the count-in and held into the song.
        if (index == 300) require(port.push(key(PluginEvent::Type::note_on, 67, 0.7)), "push");
        // Released 48128 samples into the song (block 563 starts at 144128).
        if (index == 563) require(port.push(key(PluginEvent::Type::note_off, 67, 0.0)), "push");
        const auto count = std::min<std::size_t>(block, left.size() - at);
        engine.process({std::span{left}.subspan(at, count), std::span{right}.subspan(at, count)});
    }
    // The instrument played what was played into the count-in, between clicks.
    require(probe::peak(std::span<const float>{left}.subspan(40 * block, block)) > 0.01F,
            "a note played into the count-in is heard");
    require(probe::peak(std::span<const float>{left}.subspan(200 * block, block)) == 0.0F,
            "and let go when it is released");
    require(probe::peak(std::span<const float>{left}.subspan(350 * block, block)) > 0.01F &&
                probe::peak(std::span<const float>{left}.subspan(400 * block, block)) > 0.01F,
            "the note held into the song sounds before and after the song starts");

    std::vector<CapturedEvent> captured;
    CapturedEvent event;
    while (engine.take_captured(event)) captured.push_back(event);
    require(captured.size() == 2,
            "only the held note is captured, not the one let go in the count-in: " +
                std::to_string(captured.size()) + " events");
    require(captured[0].event.type == PluginEvent::Type::note_on &&
                captured[0].event.key_or_parameter == 67 && captured[0].sample == 0 &&
                captured[0].tick == 0 && std::abs(captured[0].event.value - 0.7) < 1e-9,
            "the held note starts where the song starts, at its velocity");
    require(captured[1].event.type == PluginEvent::Type::note_off &&
                captured[1].event.key_or_parameter == 67 && captured[1].sample == 48128,
            "its release is where it was heard: sample " + std::to_string(captured[1].sample));

    // And a take made of them is one note from the song's first tick.
    TakeRecorder take(song.length());
    std::vector<PlayedNote> notes;
    for (const auto& heard : captured) {
        if (heard.event.type == PluginEvent::Type::note_on)
            take.note_on(heard.tick, static_cast<std::int16_t>(heard.event.key_or_parameter),
                         static_cast<float>(heard.event.value), heard.event.cents);
        else if (auto played = take.note_off(
                     heard.tick, static_cast<std::int16_t>(heard.event.key_or_parameter)))
            notes.push_back(*played);
    }
    require(notes.size() == 1 && notes[0].start == 0 && notes[0].key == 67 &&
                notes[0].duration == 48128 / 50,
            "the take is one note of key 67 from tick 0");
}

// ----------------------------------------------------------------- records

// features/metronome.feature: The settings are saved with the project.
void project_records_case() {
    Project project;
    // The defaults write nothing.
    require(ProjectFile::serialize(project).find("metronome") == std::string::npos,
            "a project that never touched the click saves no metronome record");
    project.song.metronome = {true, -12.5, 3};
    const auto text = ProjectFile::serialize(project);
    require(text.find("metronome on -12.5 3\n") != std::string::npos,
            "the settings are written: " + text);
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value(), "the project loads: " + error);
    require(loaded->song.metronome == project.song.metronome, "the settings load as saved");
    require(ProjectFile::serialize(*loaded) == text, "and save byte for byte the same");

    const auto base = ProjectFile::serialize(Project{});
    const auto defaults = ProjectFile::parse(base, &error);
    require(defaults.has_value() && defaults->song.metronome == MetronomeSettings{},
            "a file without the record loads with the click off and no count-in");
    for (const char* bad : {"metronome maybe -6 0", "metronome on -70 0", "metronome on 7 0",
                            "metronome on -6 5", "metronome on -6 -1", "metronome on -6",
                            "metronome on nan 0"}) {
        error.clear();
        require(!ProjectFile::parse(base + bad + "\n", &error).has_value() && !error.empty(),
                std::string("a malformed record is refused: ") + bad);
    }
    error.clear();
    require(!ProjectFile::parse(base + "metronome on -6 1\nmetronome off -6 0\n", &error)
                 .has_value(),
            "two metronome records are refused");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "pass a case name");
        const std::string name = argv[1];
        if (name == "clicks_on_tempo_step") clicks_on_tempo_step_case();
        else if (name == "clicks_in_seven_eight") clicks_in_seven_eight_case();
        else if (name == "click_level_and_switch") click_level_and_switch_case();
        else if (name == "click_ignores_solo_and_mute") click_ignores_solo_and_mute_case();
        else if (name == "bounce_click_option") bounce_click_option_case();
        else if (name == "count_in_bars") count_in_bars_case();
        else if (name == "count_in_recording") count_in_recording_case();
        else if (name == "project_records") project_records_case();
        else throw std::runtime_error("unknown case " + name);
        std::cout << name << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
