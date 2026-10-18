// features/comping_and_punch.feature: punch through the production callback,
// and comping proved from the engine's render.
#include "engine_graph.hpp"

#include "blokkily/audio/audio_asset.hpp"
#include "blokkily/audio/audio_clips.hpp"
#include "blokkily/audio/audio_input.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/comping.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/take_writer.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/plugins/sandbox.hpp"
#include "blokkily/project/project.hpp"

#include <algorithm>
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

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 512;

fs::path work(const std::string& name) {
    const auto dir = fs::path(BLOKKILY_COMPING_WORK) / name;
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir);
    return dir;
}

Song empty_song(std::size_t tracks) {
    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks.assign(tracks, Track{});
    song.clips = {};
    return song;
}

// The production callback for `frames`, with input channel c at frame f
// playing `signal(f)` (the same on every channel).
void pump(RtAudioOutput& output, std::size_t frames, std::uint64_t first,
          float (*signal)(std::uint64_t)) {
    const auto channels = output.input_channels();
    for (std::size_t done = 0; done < frames;) {
        const auto count = std::min<std::size_t>(333, frames - done);
        std::vector<float> stereo(count * 2);
        std::vector<float> injected(channels * count);
        for (std::uint32_t channel = 0; channel < channels; ++channel)
            for (std::size_t frame = 0; frame < count; ++frame)
                injected[channel * count + frame] = signal(first + done + frame);
        require(output.pump(stereo, injected), "the production callback renders");
        done += count;
    }
}

float ramp(std::uint64_t frame) { return static_cast<float>(frame % 1000) / 1000.0F; }

void punch_audio() {
    const auto dir = work("punch_audio");
    auto song = empty_song(1);
    song.clips = {{0, 0, 0, 8}};
    song.tracks[0].input = {true, TrackInput::Source::audio, -1, 0, 1, TrackInput::Monitor::off};
    SongEngine engine;
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    RtAudioOutput output{RtAudioOutput::Mode::deterministic};
    output.model_input(2);
    require(output.open(engine) && output.start(), "the output starts");
    TakeWriter writer;
    writer.begin(dir / "takes", static_cast<std::uint32_t>(rate));
    engine.connect_capture(&writer.ring());
    engine.set_audio_inputs(audio_input_routes(song, 0));
    // Punch in at 2000, out at 4000; recording runs from 1000 to 6000.
    engine.set_punch(true, 2000, 4000);
    engine.set_recording(true);
    engine.set_playing(true);
    engine.seek(1000);
    pump(output, 5000, 1000, ramp);
    auto takes = writer.finish();
    require(takes.size() == 1, "one take, inside the punch range");
    const auto& take = takes[0];
    std::cout << "punched take: " << take.first_sample << " +" << take.frames << '\n';
    require(take.first_sample == 2000 && take.frames == 2000, "it starts at punch in and ends at punch out");
    const auto wave = read_wave(take.file, &error);
    require(wave.has_value(), error);
    for (std::uint64_t frame = 0; frame < take.frames; ++frame)
        require(wave->interleaved[frame] == ramp(2000 + frame), "it holds what was played there");

    // Looped from 1000 to 5000 over the same punch: a take a pass.
    TakeWriter looped;
    looped.begin(dir / "looped", static_cast<std::uint32_t>(rate));
    engine.connect_capture(&looped.ring());
    engine.set_loop(true, 1000, 5000);
    engine.seek(1000);
    pump(output, 12000, 0, ramp);
    takes = looped.finish();
    require(takes.size() == 3, "three passes, three takes: got " + std::to_string(takes.size()));
    for (const auto& pass : takes)
        require(pass.first_sample == 2000 && pass.frames == 2000, "each pass is the punch range");
}

void punch_midi() {
    auto song = empty_song(1);
    song.clips = {{0, 0, 0, 8}};
    SongEngine engine;
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    RtAudioOutput output{RtAudioOutput::Mode::deterministic};
    require(output.open(engine) && output.start(), "the output starts");
    engine.set_punch(true, 2048, 4096);
    engine.set_recording(true);
    engine.set_playing(true);
    std::vector<float> stereo(block * 2);
    // A note struck in every block: before, inside and after the range.
    for (int pass = 0; pass < 12; ++pass) {
        require(engine.perform(0, {PluginEvent::Type::note_on, 0, 60 + pass, 0.8, 0.0, 0}), "perform");
        require(engine.perform(0, {PluginEvent::Type::note_off, 0, 60 + pass, 0.0, 0.0, 0}), "perform");
        require(output.pump(stereo), "pump");
    }
    std::vector<int> struck;
    int lifted = 0;
    CapturedEvent event;
    while (engine.take_captured(event)) {
        if (event.event.type == PluginEvent::Type::note_on) {
            struck.push_back(event.event.key_or_parameter);
            require(event.sample >= 2048 && event.sample < 4096, "a captured note is inside the range");
        } else if (event.event.type == PluginEvent::Type::note_off) {
            ++lifted;
        }
    }
    std::cout << "punched notes:";
    for (const int key : struck) std::cout << ' ' << key;
    std::cout << '\n';
    require(struck == std::vector<int>{64, 65, 66, 67}, "only the notes struck between punch in and out");
    require(lifted == 12, "every note-off reaches the take, so none hangs");
}

// A file of `frames` frames at a steady `level`.
AudioFileRef steady(const fs::path& file, float level, std::uint64_t frames) {
    std::vector<float> samples(frames, level);
    WaveWriter writer;
    std::string error;
    require(writer.open(file, static_cast<std::uint32_t>(rate), WaveFormat::float32, &error) &&
                writer.write(samples, samples, &error) && writer.close(&error), error);
    return {file, frames, static_cast<std::uint32_t>(rate), 2};
}

AudioClipId add_take(Song& song, std::size_t file, Tick start, std::uint64_t frames) {
    AudioClip clip;
    clip.id = song.next_audio_clip_id();
    clip.track = 0;
    clip.file = file;
    clip.start = start;
    clip.length_frames = frames;
    song.audio_clips.push_back(clip);
    return clip.id;
}

const AudioClip& clip_of(const Song& song, AudioClipId id) {
    return *std::find_if(song.audio_clips.begin(), song.audio_clips.end(),
                         [id](const AudioClip& clip) { return clip.id == id; });
}

void comp_passes() {
    const auto dir = work("comp_passes");
    auto song = empty_song(1);
    song.audio_files.push_back(steady(dir / "a.wav", 0.1F, 48000));
    const auto earlier = add_take(song, 0, 0, 48000);   // in the song before
    // Three passes over the second half of it (480 ticks is half a second).
    const auto one = add_take(song, 0, 480, 24000);
    const auto two = add_take(song, 0, 480, 24000);
    const auto three = add_take(song, 0, 480, 24000);
    const auto elsewhere = add_take(song, 0, 20000, 4800); // after all of them
    require(take_stack(song, two).size() == 4, "the takes of the same bars are a stack");
    require(comp_recorded_takes(song, {one, two, three}) == 2, "two earlier passes are kept");
    require(clip_of(song, one).comped_out && clip_of(song, two).comped_out &&
                !clip_of(song, three).comped_out, "the last pass plays");
    require(!clip_of(song, earlier).comped_out && !clip_of(song, elsewhere).comped_out,
            "clips from before the recording are left as they were");
    require(choose_take(song, one), "a take can be chosen");
    require(!clip_of(song, one).comped_out && clip_of(song, three).comped_out &&
                clip_of(song, earlier).comped_out && !clip_of(song, elsewhere).comped_out,
            "choosing one comps out everything it overlaps, nothing else");
}

// The song's bounce, left channel.
std::vector<float> bounce(const Song& song, const fs::path& file) {
    SongEngine engine;
    AudioAssetCache cache;
    ClipAssetReport report;
    const auto assets = load_clip_assets(song, cache, rate, &report);
    require(report.missing_count() == 0, "the files load");
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error, assets), error);
    require(bounce_song(engine, file, WaveFormat::float32, 0, &error).has_value(), error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value(), error);
    std::vector<float> left(wave->frames);
    for (std::size_t i = 0; i < left.size(); ++i) left[i] = wave->interleaved[2 * i];
    return left;
}

void comp_render() {
    const auto dir = work("comp_render");
    auto song = empty_song(1);
    song.audio_files.push_back(steady(dir / "a.wav", 0.1F, 48000));
    song.audio_files.push_back(steady(dir / "b.wav", 0.3F, 48000));
    // Two takes of the same second (960 ticks at 120 BPM, 480 a beat), A
    // recorded first, B second.
    const auto a = add_take(song, 0, 0, 48000);
    const auto b = add_take(song, 1, 0, 48000);
    // A centred strip passes cos(pi/4) of the signal to each side.
    const auto level = [](const std::vector<float>& left, std::size_t at) {
        return left.at(at) / 0.70710678F;
    };
    auto summed = bounce(song, dir / "summed.wav");
    require(std::abs(level(summed, 1000) - 0.4F) < 1e-5F, "uncomped, overlapping clips sum, as before");
    (void)comp_recorded_takes(song, {a, b});
    auto latest = bounce(song, dir / "latest.wav");
    require(std::abs(level(latest, 1000) - 0.3F) < 1e-5F, "comped, only the chosen take B is heard");
    require(choose_take(song, a), "choose A");
    auto chosen = bounce(song, dir / "chosen.wav");
    require(std::abs(level(chosen, 1000) - 0.1F) < 1e-5F && std::abs(level(chosen, 40000) - 0.1F) < 1e-5F,
            "A chosen, A is heard throughout");
    // Split at half a second (480 ticks = 24000 samples) and play B after it.
    require(split_takes(song, 0, 480) == 2, "both takes are cut at the playhead");
    const auto second_half_b = std::find_if(song.audio_clips.begin(), song.audio_clips.end(),
                                            [](const AudioClip& clip) { return clip.file == 1 && clip.start == 480; });
    require(second_half_b != song.audio_clips.end() && second_half_b->offset_frames == 24000 &&
                second_half_b->length_frames == 24000 && second_half_b->comped_out,
            "B's second half starts half a second into its file, still comped out");
    require(choose_take(song, second_half_b->id), "choose B for the second half");
    auto comped = bounce(song, dir / "comped.wav");
    std::cout << "comped: " << level(comped, 23999) << " then " << level(comped, 24000) << '\n';
    require(std::abs(level(comped, 1000) - 0.1F) < 1e-5F && std::abs(level(comped, 23999) - 0.1F) < 1e-5F,
            "the first half is A to its last sample");
    require(std::abs(level(comped, 24000) - 0.3F) < 1e-5F && std::abs(level(comped, 47000) - 0.3F) < 1e-5F,
            "the second half is B from the cut on");
    // A take that continues across a cut is seamless: A's halves chosen.
    require(choose_take(song, std::find_if(song.audio_clips.begin(), song.audio_clips.end(),
                                           [](const AudioClip& clip) { return clip.file == 0 && clip.start == 480; })->id),
            "choose A again for the second half");
    auto whole = bounce(song, dir / "whole.wav");
    for (std::size_t frame = 23990; frame < 24010; ++frame)
        require(std::abs(level(whole, frame) - 0.1F) < 1e-5F, "A across the cut has no seam");
}

void records() {
    const auto dir = work("records");
    Project project;
    project.song = empty_song(1);
    project.song.audio_files.push_back(steady(dir / "a.wav", 0.1F, 4800));
    const auto a = add_take(project.song, 0, 0, 4800);
    (void)add_take(project.song, 0, 0, 4800);
    project.song.audio_clips[0].comped_out = true;
    project.song.loop.punch = true;
    const auto text = ProjectFile::serialize(project, dir);
    require(text.find("comped_out " + std::to_string(a)) != std::string::npos &&
                text.find("punch on") != std::string::npos, "comping and punch are saved");
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error, dir);
    require(loaded.has_value(), error);
    require(loaded->song.audio_clips[0].comped_out && !loaded->song.audio_clips[1].comped_out &&
                loaded->song.loop.punch, "and load as saved");
    require(ProjectFile::serialize(Project{}).find("comped_out") == std::string::npos &&
                ProjectFile::serialize(Project{}).find("punch") == std::string::npos,
            "a song without them saves neither");
    require(!ProjectFile::parse(text + "comped_out 999\n", &error, dir).has_value(),
            "a clip the file does not have is refused");
}

// --- MIDI takes ----------------------------------------------------------

// The frequency a stretch of a signal sounds, from its rising zero crossings.
double frequency(const std::vector<float>& signal, std::size_t from, std::size_t to) {
    std::size_t first = 0, last = 0, crossings = 0;
    for (std::size_t index = from + 1; index < to; ++index)
        if (signal[index - 1] <= 0.0F && signal[index] > 0.0F) {
            if (crossings == 0) first = index;
            last = index;
            ++crossings;
        }
    if (crossings < 2) return 0.0;
    return static_cast<double>(crossings - 1) * rate / static_cast<double>(last - first);
}

// A take section of one bar whose part on track 0 holds `key` at both halves
// of the bar, placed as the take that plays.
std::size_t add_take_pass(Song& song, int key) {
    const auto section = song.add_section("VERSE TAKE", 1920, 480);
    song.sections[section].take = true;
    const auto part = *song.part(section, 0);
    for (Tick start : {Tick{0}, Tick{960}}) {
        Trigger note;
        note.start = start;
        note.duration = 900;
        note.musical_data = Note{static_cast<std::int16_t>(key), 1.0F, 0.0F};
        (void)song.patterns[part].pattern.add(note);
    }
    return place_midi_take(song, 0, part, 0);
}

Song take_song() {
    Song song;
    song.patterns.clear();
    song.clips.clear();
    song.tracks.assign(1, Track{});
    song.tracks[0].name = "LEAD";
    song.tracks[0].instrument = {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", {}};
    // The bar the passes were recorded over: a section of its own, played,
    // with nothing in it.
    const auto verse = song.add_section("VERSE", 1920, 480);
    song.clips.push_back({0, *song.part(verse, 0), 0, 1});
    return song;
}

// The song bounced by the engine with the real CLAP synth, left channel.
std::vector<float> render_bar(const Song& song) {
    SongEngine engine;
    std::string error;
    auto voice = create_format_processor(song.tracks[0].instrument, &error);
    require(voice != nullptr, "the CLAP synth loads: " + error);
    engine.set_instrument(0, std::move(voice));
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
    // The synth's Tone (parameter 1) on: a sine at each key's pitch.
    require(engine.play_live(0, {PluginEvent::Type::parameter_value, 0, 1, 1.0}), "Tone on");
    const auto file = work("midi-takes") / "bar.wav";
    require(bounce_song(engine, file, WaveFormat::float32, 0, &error).has_value(), error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value(), error);
    std::vector<float> left(wave->frames);
    for (std::size_t i = 0; i < left.size(); ++i) left[i] = wave->interleaved[2 * i];
    return left;
}

// Scenario: Looped MIDI passes are takes; the last plays
void midi_takes() {
    auto song = take_song();
    const auto first = add_take_pass(song, 60);
    const auto second = add_take_pass(song, 67);
    require(song.consistent() && song.sections_whole(), "takes keep the song's sections whole");
    const auto stack = midi_take_stack(song, second);
    require(stack == std::vector<std::size_t>({first, second}), "the passes are one stack");
    require(song.clips[first].comped_out && !song.clips[second].comped_out && !song.clips[0].comped_out,
            "the last pass plays, the earlier is kept, and the bar they were recorded over plays on");
    require(!is_midi_take(song, 0), "which is not a take");
    // 120 BPM: a bar is 96000 samples, each half 48000.
    const double c4 = 261.63, g4 = 392.0;
    auto heard = render_bar(song);
    const double last = frequency(heard, 2000, 40000);
    std::cout << "midi takes: last pass sounds " << last << " Hz\n";
    require(std::abs(last - g4) < 3.0, "the last pass is heard");
    require(choose_midi_take(song, first) && song.clips[second].comped_out, "choosing the first");
    heard = render_bar(song);
    require(std::abs(frequency(heard, 2000, 40000) - c4) < 3.0, "plays the first");
    // Split at the half bar: the first pass before it, the second after.
    require(split_midi_takes(song, 0, 960) == 2, "both takes cut at the half bar");
    std::size_t second_tail = song.clips.size();
    for (std::size_t index = 0; index < song.clips.size(); ++index)
        if (song.clips[index].start == 960 && song.clips[index].pattern == song.clips[second].pattern)
            second_tail = index;
    require(second_tail < song.clips.size() && choose_midi_take(song, second_tail), "the second's tail");
    heard = render_bar(song);
    const double before = frequency(heard, 2000, 40000), after = frequency(heard, 50000, 88000);
    std::cout << "midi takes: split " << before << " Hz then " << after << " Hz\n";
    require(std::abs(before - c4) < 3.0 && std::abs(after - g4) < 3.0,
            "the first pass to the cut, the second from it");
}

// Scenario: MIDI takes are saved
void midi_take_records() {
    const auto dir = work("midi-take-records");
    Project project;
    project.song = take_song();
    project.song.tracks[0].instrument = {};
    const auto first = add_take_pass(project.song, 60);
    (void)add_take_pass(project.song, 67);
    project.song.loop.midi_takes = false;
    const auto text = ProjectFile::serialize(project, dir);
    require(text.find("comped_out_clip " + std::to_string(first)) != std::string::npos &&
                text.find("take_section 1") != std::string::npos &&
                text.find("midi_takes off") != std::string::npos,
            "comped-out takes, take sections and merging are saved");
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error, dir);
    require(loaded.has_value(), error);
    require(loaded->song.clips[first].comped_out && loaded->song.sections[1].take &&
                loaded->song.sections[2].take && !loaded->song.sections[0].take &&
                !loaded->song.loop.midi_takes,
            "and load as saved");
    const auto plain = ProjectFile::serialize(Project{});
    require(plain.find("comped_out_clip") == std::string::npos &&
                plain.find("take_section") == std::string::npos &&
                plain.find("midi_takes") == std::string::npos,
            "a song without takes saves none of them");
    require(!ProjectFile::parse(text + "comped_out_clip 99\n", &error, dir).has_value() &&
                !ProjectFile::parse(text + "take_section 99\n", &error, dir).has_value(),
            "a clip or a section the file does not have is refused");
}

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "case required");
        const std::string which = argv[1];
        if (which == "punch_audio") punch_audio();
        else if (which == "punch_midi") punch_midi();
        else if (which == "comp_passes") comp_passes();
        else if (which == "comp_render") comp_render();
        else if (which == "records") records();
        else if (which == "midi_takes") midi_takes();
        else if (which == "midi_take_records") midi_take_records();
        else throw std::runtime_error("unknown case");
        std::cout << "PASS " << which << '\n';
    } catch (const std::exception& e) {
        std::cerr << "FAIL " << e.what() << '\n';
        return 1;
    }
}
