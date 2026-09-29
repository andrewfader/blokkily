// Executable scenarios for features/scene_launcher.feature (phase 2, wave
// 6.1). Each case is its own CTest test, scene_launcher_<case>.
//
// Every claim about playback is read off the master bus the production
// SongEngine::process() rendered, with the CLAP fixture loaded through the
// production adapter as every track's instrument. The fixture is put in its
// velocity mode by a parameter lock on every note, so it sounds 0.25 x the sum
// of the velocities of the keys held: which cell is playing, and exactly on
// which sample a note starts or is let go, can be read from the samples.

#include "support/audio_probe.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/model/scene_launcher.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/project/project.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string text(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 256;
// 120 bpm, 480 ticks a beat, 48 kHz: 50 samples a tick.
constexpr std::uint64_t per_tick = 50;
constexpr std::uint64_t bar = 1920 * per_tick;
constexpr std::uint64_t beat = 480 * per_tick;

// What one held key of velocity `velocity` sounds at on the master, through
// a centred strip.
double level(double velocity) {
    const auto gain = strip_gain(MixerStrip{}, false);
    return 0.25 * velocity * gain.left;
}

bool near(double actual, double expected, double tolerance = 1e-5) {
    return std::abs(actual - expected) <= tolerance;
}

struct NoteSpec {
    Tick start = 0;
    Tick duration = 480;
    std::int16_t key = 60;
    float velocity = 0.5F;
    std::uint8_t play_on_loop = 0;
};

Pattern notes(std::initializer_list<NoteSpec> specs, Tick length = 1920) {
    Pattern pattern(length, 480);
    for (const auto& spec : specs) {
        Trigger trigger;
        trigger.start = spec.start;
        trigger.duration = spec.duration;
        trigger.musical_data = Note{spec.key, spec.velocity, 0.0F};
        trigger.play_on_loop = spec.play_on_loop;
        trigger.locks = {{"velocity-mode", 2, 1.0, ParameterLock::Kind::automation}};
        require(pattern.add(trigger) != 0, "a note is added to the pattern");
    }
    return pattern;
}

std::unique_ptr<PluginInstance> synth() {
    std::string error;
    auto instance = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
    require(instance != nullptr, "the CLAP fixture loads through the production adapter: " + error);
    return instance;
}

// A song of `tracks` tracks and `bars` bars: the length comes from an empty
// pattern's clip on the last track, so no launched cell is ever wrapped by
// the song ending.
Song session(std::size_t tracks, std::uint32_t bars) {
    Song song;
    song.patterns = {{"EMPTY", Pattern(1920, 480)}};
    song.tracks.assign(tracks, Track{});
    song.clips = {{tracks - 1, 0, 0, bars}};
    return song;
}

void prepare(SongEngine& engine, const Song& song) {
    for (std::size_t track = 0; track < song.tracks.size(); ++track)
        engine.set_instrument(track, synth());
    std::string error;
    require(engine.prepare(song, rate, 1024, 0, &error), "prepare: " + error);
}

// Renders `frames` samples of the master's left channel in blocks of `size`,
// calling `before` with the position of each block before it is rendered.
std::vector<float> render(SongEngine& engine, std::uint64_t frames,
                          const std::function<void(std::uint64_t)>& before = {},
                          std::uint32_t size = block) {
    std::vector<float> left(frames), right(frames);
    for (std::uint64_t done = 0; done < frames;) {
        const auto now = static_cast<std::size_t>(std::min<std::uint64_t>(size, frames - done));
        if (before) before(done);
        engine.process({std::span(left).subspan(done, now), std::span(right).subspan(done, now)});
        done += now;
    }
    return left;
}

// The first sample at or after `from` whose level differs from silence.
std::uint64_t first_sound(const std::vector<float>& out, std::uint64_t from) {
    for (auto at = from; at < out.size(); ++at)
        if (std::abs(out[at]) > 1e-9F) return at;
    return out.size();
}

bool silent(const std::vector<float>& out, std::uint64_t from, std::uint64_t to) {
    for (auto at = from; at < to && at < out.size(); ++at)
        if (std::abs(out[at]) > 1e-9F) return false;
    return true;
}

bool steady(const std::vector<float>& out, std::uint64_t from, std::uint64_t to, double value) {
    for (auto at = from; at < to && at < out.size(); ++at)
        if (!near(out[at], value)) return false;
    return true;
}

// features/scene_launcher.feature: A quantized scene launch starts on the bar
// boundary sample; a cell launched on its own waits for its own
// quantization; a stop waits for the grid's.
void quantized_launch_case() {
    auto song = session(2, 8);
    song.patterns.push_back({"LOW", notes({{0, 960, 60, 0.4F}})});
    song.patterns.push_back({"HIGH", notes({{0, 960, 64, 0.8F}})});
    song.patterns.push_back({"LONG", notes({{0, 1900, 67, 0.6F}})});
    song.launcher.add_scene("A");
    song.launcher.add_scene("B");
    song.launcher.set_slot(0, 0, {.pattern = 1});
    song.launcher.set_slot(0, 1, {.pattern = 2});
    song.launcher.set_slot(1, 1, {.pattern = 3, .quantization = LaunchQuantization::beat});
    require(song.consistent(), "the song is valid");

    SongEngine engine;
    prepare(engine, song);
    engine.set_playing(true);
    bool queued_seen = false;
    const auto out = render(engine, 4 * bar, [&](std::uint64_t at) {
        // Launched well inside bar 0; the scene waits for bar 1.
        if (at == 117 * block) require(engine.launch_scene(0), "the scene launch is queued");
        if (at == 118 * block) {
            const auto status = engine.launcher_status(0);
            queued_seen = status.queued_play && !status.playing && status.queued_scene == 0;
        }
        // Stopped inside bar 2 with the grid's bar quantization: at bar 3.
        if (at == 850 * block) require(engine.stop_all_launched(), "the stop is queued");
        // Scene B's cell on track 1 alone, inside bar 3: it waits for a beat.
        if (at == 1172 * block) require(engine.launch_cell(1, 1), "the cell launch is queued");
    });
    require(queued_seen, "the launched scene is shown queued before its boundary");
    const double both = level(0.4) + level(0.8);
    require(first_sound(out, 0) == bar,
            "nothing sounds until the bar boundary, then both cells on it exactly: first sound at " +
                std::to_string(first_sound(out, 0)));
    require(near(out[bar], both) && steady(out, bar, bar + 960 * per_tick, both),
            "both tracks sound from the boundary sample, got " + text(out[bar]));
    require(silent(out, bar + 960 * per_tick, 2 * bar), "the notes end where the pattern says");
    require(near(out[2 * bar], both), "the cells loop into bar 2");
    require(silent(out, 3 * bar, 3 * bar + 12000),
            "the quantized stop silences both tracks on bar 3 exactly");
    // 1172 blocks is 300032 samples, inside bar 3's first beat: the cell's
    // beat quantization starts it on the second beat.
    const auto start = 3 * bar + beat;
    require(first_sound(out, 3 * bar) == start && near(out[start], level(0.6)),
            "a cell launched alone waits for its own quantization, a beat: first sound at " +
                std::to_string(first_sound(out, 3 * bar)));
    const auto status = engine.launcher_status(1);
    require(status.playing && status.scene == 1 && !engine.launcher_status(0).playing,
            "the status names what plays");
}

// A note on the downbeat of every loop is played on every loop, whether the
// loop turns inside a block or on its edge, and whether or not the song
// wraps under it. (Regression: the first engine dropped a downbeat that fell
// in the same block as the loop's turn.)
void downbeat_after_wrap_case() {
    // The song is 2500 ticks, so it wraps at 125000 samples, out of step with
    // the launched one-bar loop.
    Song song;
    song.patterns = {{"ODD", Pattern(2500, 480)}, {"BEAT", notes({{0, 240, 60, 1.0F},
                                                                   {960, 240, 62, 0.5F}})}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};
    song.launcher.add_scene("S");
    song.launcher.set_slot(0, 0, {.pattern = 1, .quantization = LaunchQuantization::none});
    SongEngine engine;
    prepare(engine, song);
    require(engine.song_samples() == 2500 * per_tick, "the song is 2500 ticks");
    require(engine.launch_scene(0), "launched");
    engine.set_playing(true);
    // Blocks of 700 samples: the loop's turn and the song's wrap fall inside
    // a block.
    const auto out = render(engine, 6 * bar, {}, 700);
    for (std::uint64_t loop = 0; loop < 6; ++loop) {
        const auto start = loop * bar;
        require(near(out[start], level(1.0)),
                "the downbeat of loop " + std::to_string(loop) + " sounds on its first sample, got " +
                    text(out[start]));
        if (loop > 0) require(near(out[start - 1], 0.0), "silence before the downbeat");
        require(near(out[start + 960 * per_tick], level(0.5)), "the offbeat of every loop sounds");
    }
    require(probe::rising_edges(out, 1e-4F) == 12, "exactly two notes a loop for six loops");
}

// Follow actions: `next` after its repeats, `random` into another scene, and
// `first` back to the top, heard from which cell's level plays.
void follow_actions_case() {
    auto song = session(1, 16);
    song.tracks.push_back(Track{});
    song.clips = {{1, 0, 0, 16}};
    song.patterns.push_back({"A", notes({{0, 960, 60, 0.4F}})});
    song.patterns.push_back({"B", notes({{0, 960, 60, 0.8F}})});
    song.patterns.push_back({"C", notes({{0, 960, 60, 0.6F}})});
    song.launcher.add_scene("A");
    song.launcher.add_scene("B");
    song.launcher.add_scene("C");
    song.launcher.set_slot(0, 0, {.pattern = 1, .repeats = 2, .quantization = LaunchQuantization::none,
                                  .follow_action = FollowAction::next});
    song.launcher.set_slot(1, 0, {.pattern = 2, .repeats = 1, .quantization = LaunchQuantization::none,
                                  .follow_action = FollowAction::random});
    song.launcher.set_slot(2, 0, {.pattern = 3, .repeats = 1, .quantization = LaunchQuantization::none,
                                  .follow_action = FollowAction::first});
    SongEngine engine;
    prepare(engine, song);
    require(engine.launch_cell(0, 0), "launched");
    engine.set_playing(true);
    // Blocks of 700 samples: every loop turns inside a block.
    const auto out = render(engine, 8 * bar, {}, 700);
    std::vector<double> heard;
    for (std::uint64_t loop = 0; loop < 8; ++loop) heard.push_back(out[loop * bar + 10]);
    require(near(heard[0], level(0.4)) && near(heard[1], level(0.4)), "A plays its two repeats");
    require(near(heard[2], level(0.8)), "then next: B");
    // Random leaves B for another scene with a cell: A or C, never B again.
    require(near(heard[3], level(0.4)) || near(heard[3], level(0.6)),
            "random goes to another scene, got " + text(heard[3]));
    int visits_to_c = 0;
    for (std::uint64_t loop = 3; loop < 8; ++loop) {
        require(near(heard[loop], level(0.4)) || near(heard[loop], level(0.6)) ||
                    near(heard[loop], level(0.8)),
                "every loop plays one of the cells");
        if (near(heard[loop], level(0.6))) {
            ++visits_to_c;
            // C's follow action is first: A follows it.
            if (loop + 1 < 8) require(near(heard[loop + 1], level(0.4)), "first returns to A");
        }
    }
    // Two runs from the same seed follow the same path.
    SongEngine again;
    prepare(again, song);
    require(again.launch_cell(0, 0), "launched again");
    again.set_playing(true);
    const auto repeat = render(again, 8 * bar, {}, 700);
    for (std::uint64_t loop = 0; loop < 8; ++loop)
        require(near(repeat[loop * bar + 10], heard[loop]), "the random follow action is seeded");
    std::cerr << "follow actions: C visited " << visits_to_c << " times\n";
}

// A follow action that stops a track, a quantized stop and the transport
// stopping all let go of what the cell holds: the audio falls to silence on
// the boundary rather than ringing on.
void stop_releases_case() {
    auto song = session(1, 16);
    song.tracks.push_back(Track{});
    song.clips = {{1, 0, 0, 16}};
    // The note would ring from the middle of the bar well into the next.
    song.patterns.push_back({"HELD", notes({{960, 1900, 60, 1.0F}})});
    song.launcher.add_scene("ONCE");
    song.launcher.add_scene("LOOP");
    song.launcher.set_slot(0, 0, {.pattern = 1, .repeats = 1, .quantization = LaunchQuantization::none,
                                  .follow_action = FollowAction::stop});
    song.launcher.set_slot(1, 0, {.pattern = 1, .quantization = LaunchQuantization::none});
    SongEngine engine;
    prepare(engine, song);
    require(engine.launch_cell(0, 0), "launched");
    engine.set_playing(true);
    const auto out = render(engine, 6 * bar, [&](std::uint64_t at) {
        // 1500 blocks is 384000, bar 4 exactly: the loop starts there.
        if (at == 1500 * block) require(engine.launch_cell(1, 0), "launched the loop");
        // A quantized stop (the grid's bar) while the note rings: bar 5.
        if (at == 1700 * block) require(engine.stop_launched(0), "stop queued");
    });
    require(near(out[bar - 1], level(1.0)), "the note rings up to the end of the loop");
    require(silent(out, bar, 4 * bar),
            "stop as a follow action releases the note on the boundary");
    require(first_sound(out, 4 * bar) == 4 * bar + 960 * per_tick, "the loop plays");
    require(near(out[5 * bar - 1], level(1.0)) && silent(out, 5 * bar, 6 * bar),
            "the quantized stop releases the ringing note on bar 5 exactly");

    // The transport stopping while the cell holds a note lets it go too.
    require(engine.launch_cell(1, 0), "launched again");
    const auto held = render(engine, bar);
    require(near(held.back(), level(1.0)), "the relaunched note rings");
    engine.set_playing(false);
    const auto stopped = render(engine, 4 * block);
    require(silent(stopped, 0, stopped.size()), "stopping the transport releases the launched note");
    require(!engine.launcher_status(0).playing, "and stops the track");
}

// Arrangement recording: the takes, printed into the song, play back and
// export exactly what the launcher played, down to the sample, including a
// take cut part-way through a loop and a pattern whose loops differ.
void record_prints_arrangement_case() {
    auto song = session(2, 16);
    // P0's second note plays every other loop, so its launched loops repeat
    // every two; P1's note is cut by the stop.
    song.patterns.push_back({"P0", notes({{0, 480, 60, 0.4F}, {960, 480, 62, 0.6F, 2}})});
    song.patterns.push_back({"P1", notes({{480, 1200, 64, 0.8F}})});
    song.launcher.add_scene("VERSE");
    song.launcher.add_scene("CHORUS");
    song.launcher.quantization = LaunchQuantization::none;
    song.launcher.set_slot(0, 0, {.pattern = 1});
    song.launcher.set_slot(1, 0, {.pattern = 2});
    require(song.consistent(), "valid");

    SongEngine live;
    prepare(live, song);
    live.set_launcher_recording(true);
    require(live.launch_cell(0, 0), "launched at the transport's start");
    live.set_playing(true);
    const std::uint64_t cut_block = 419840 / block;
    const auto heard = render(live, 6 * bar, [&](std::uint64_t at) {
        // Inside loop 3: CHORUS takes over on the next bar (the cell's
        // quantization).
        if (at == 1080 * block) require(live.launch_cell(1, 0), "chorus launched");
        // Mid-note in CHORUS's second loop, with the grid's quantization
        // none: the take is cut there.
        if (at == cut_block * block) require(live.stop_launched(0), "stopped");
    });
    std::vector<LauncherTake> takes;
    LauncherTake take;
    while (live.take_launcher_take(take)) takes.push_back(take);
    require(takes.size() == 2, "two takes: " + std::to_string(takes.size()));
    require(takes[0].track == 0 && takes[0].pattern == 1 && takes[0].start == 0 &&
                takes[0].end == 3 * 1920 && takes[0].cycle == 2,
            "VERSE was played for three loops of a two-loop cycle");
    require(takes[1].pattern == 2 && takes[1].start == 3 * 1920 && takes[1].end > 4 * 1920 + 480 &&
                takes[1].end < 5 * 1920,
            "CHORUS from bar 3 to the stop, " + std::to_string(takes[1].end));
    const auto cut = static_cast<std::uint64_t>(takes[1].end) * per_tick;
    require(near(heard[cut - 1], level(0.8)) && silent(heard, cut, heard.size()),
            "the stop cut CHORUS's note where the take ends");

    auto printed = song;
    for (const auto& one : takes)
        require(printed.print_take(one.track, one.pattern, one.start, one.end, one.cycle),
                "the take prints");
    require(printed.consistent(), "the printed song is valid");
    std::vector<Clip> on_track;
    for (const auto& clip : printed.clips)
        if (clip.track == 0) on_track.push_back(clip);
    require(on_track.size() == 3, "three clips: two cycles of VERSE and one CHORUS");
    require(on_track[0].start == 0 && on_track[0].repeats == 2 && on_track[0].length == 0 &&
                on_track[1].start == 2 * 1920 && on_track[1].repeats == 1 &&
                on_track[2].start == 3 * 1920 && on_track[2].repeats == 2 &&
                on_track[2].length == takes[1].end - 3 * 1920,
            "the clips follow the takes");

    // The printed arrangement, played by a fresh engine with no launch.
    SongEngine playback;
    prepare(playback, printed);
    playback.set_playing(true);
    const auto played = render(playback, 6 * bar);
    std::uint64_t differing = 0;
    for (std::size_t at = 0; at < heard.size(); ++at)
        if (heard[at] != played[at]) ++differing;
    require(differing == 0, "the printed arrangement plays what the launcher played; " +
                                std::to_string(differing) + " samples differ");
    require(probe::rising_edges(played, 1e-4F) >= 5, "and it is not silence");

    // Exported, read back: the same samples again.
    std::string error;
    std::filesystem::create_directories(BLOKKILY_TEST_ARTIFACTS);
    const auto file = std::filesystem::path(BLOKKILY_TEST_ARTIFACTS) / "scene_launcher_print.wav";
    require(bounce_song(playback, file, WaveFormat::float32, 0, &error).has_value(),
            "the bounce is written: " + error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value() && wave->channels == 2 && wave->frames >= heard.size(),
            "the bounce reads back: " + error);
    differing = 0;
    for (std::size_t at = 0; at < heard.size(); ++at)
        if (wave->interleaved[at * 2] != heard[at]) ++differing;
    require(differing == 0, "the export is what the launcher played; " + std::to_string(differing) +
                                " samples differ");
}

// Editing the song while a cell plays reaches the running engine as a
// recompile: the edited pattern is heard on its next note, a deleted pattern
// before the cell's leaves it playing the same music, and clearing the cell
// stops the track and lets go of its note.
void edit_while_launched_case() {
    auto song = session(1, 16);
    song.tracks.push_back(Track{});
    song.clips = {{1, 0, 0, 16}};
    song.patterns.push_back({"SPARE", notes({{0, 480, 60, 1.0F}})});
    song.patterns.push_back({"PLAYED", notes({{0, 1900, 60, 0.4F}})});
    song.launcher.add_scene("S");
    song.launcher.set_slot(0, 0, {.pattern = 2, .quantization = LaunchQuantization::none});
    SongEngine engine;
    prepare(engine, song);
    require(engine.launch_cell(0, 0), "launched");
    engine.set_playing(true);
    auto out = render(engine, bar / 2);
    require(near(out.back(), level(0.4)), "the cell plays");

    // The note's velocity edited mid-loop: heard from the next loop.
    song.patterns[2].pattern = notes({{0, 1900, 60, 0.8F}});
    std::string error;
    require(engine.recompile(song, 0, &error), "recompiled: " + error);
    out = render(engine, bar);
    require(near(out[bar / 2 - 2000], level(0.4)) && near(out[bar / 2 + 10], level(0.8)),
            "the edit is heard from the next loop, and the note held through it");

    // A pattern before the played one is deleted: the cell follows it down.
    song.patterns.erase(song.patterns.begin() + 1);
    song.launcher.remove_pattern(1);
    for (auto& clip : song.clips)
        if (clip.pattern > 1) --clip.pattern;
    require(song.consistent() && song.launcher.slot(0, 0)->pattern == 1,
            "the cell names the same pattern after the list closes up");
    require(engine.recompile(song, 0, &error), "recompiled: " + error);
    out = render(engine, bar);
    require(near(out[bar / 2 + 10], level(0.8)), "and plays on as before");

    // Deleting the pattern the cell plays empties the cell: the track stops.
    song.patterns.erase(song.patterns.begin() + 1);
    song.launcher.remove_pattern(1);
    require(song.consistent() && !song.launcher.slot(0, 0).has_value(), "the cell is emptied");
    require(engine.recompile(song, 0, &error), "recompiled: " + error);
    out = render(engine, 4 * block);
    require(silent(out, 0, out.size()) && !engine.launcher_status(0).playing,
            "the emptied cell stops its track and lets go of the note at once");
}

// The grid is saved with the song, names and all, and a file that asks for
// something the launcher does not have is refused.
void serialization_case() {
    Project project;
    project.song.patterns = {{"P0", Pattern(1920, 480)}, {"P1", Pattern(1920, 480)}};
    project.song.tracks = {Track{}, Track{}};
    project.song.clips = {{0, 0, 0, 2, 2000}};
    project.song.launcher.quantization = LaunchQuantization::two_bars;
    project.song.launcher.add_scene("Verse 2");
    project.song.launcher.add_scene("");
    project.song.launcher.add_scene("100% ~ \"drop\"\tnow");
    project.song.launcher.set_slot(0, 0, {.pattern = 1, .repeats = 2,
                                          .quantization = LaunchQuantization::beat,
                                          .follow_action = FollowAction::random});
    project.song.launcher.set_slot(2, 1, {.pattern = 0, .repeats = 4,
                                          .quantization = LaunchQuantization::none,
                                          .follow_action = FollowAction::again});
    require(project.song.consistent(), "the song is valid");
    const auto text_form = ProjectFile::serialize(project);
    std::string error;
    const auto reloaded = ProjectFile::parse(text_form, &error);
    require(reloaded.has_value(), "the project loads again: " + error);
    require(reloaded->song.launcher == project.song.launcher,
            "every scene, name, cell and the grid's quantization survive");
    require(reloaded->song.clips.size() == 1 && reloaded->song.clips[0].length == 2000,
            "a cut clip keeps its cut");

    const auto refused = [&](const std::string& from, const std::string& to, const char* what) {
        auto broken = text_form;
        const auto at = broken.find(from);
        require(at != std::string::npos, std::string("the record is there: ") + what);
        broken.replace(at, from.size(), to);
        require(!ProjectFile::parse(broken, &error).has_value(), std::string("refused: ") + what);
    };
    refused("sceneslot 0 0 1 2 2 6", "sceneslot 0 0 1 2 9 6", "a quantization that does not exist");
    refused("sceneslot 0 0 1 2 2 6", "sceneslot 0 0 1 2 2 8", "a follow action that does not exist");
    refused("sceneslot 0 0 1 2 2 6", "sceneslot 0 0 7 2 2 6", "a cell of a pattern that does not exist");
    refused("launcher 4", "launcher 6", "a grid quantization that does not exist");
    refused("clip 0 0 0 2 2000", "clip 0 0 0 2 5000", "a cut past the clip's repeats");

    // A file from before names were escaped: "" for an empty name, and a
    // scene tempo after the name. It loads; the tempo is not kept.
    auto old = text_form;
    const auto launcher_line = old.find("launcher 4\n");
    require(launcher_line != std::string::npos, "the launcher record is there");
    old.erase(launcher_line, std::string("launcher 4\n").size());
    for (const auto& [from, to] : {std::pair<std::string, std::string>{"scene 0 Verse%202", "scene 0 Verse 124"},
                                   {"scene 1 ~", "scene 1 \"\" 128"}}) {
        const auto at = old.find(from);
        require(at != std::string::npos, "the scene record is there: " + from);
        old.replace(at, from.size(), to);
    }
    const auto legacy = ProjectFile::parse(old, &error);
    require(legacy.has_value(), "an older file still loads: " + error);
    require(legacy->song.launcher.scenes.size() == 3 && legacy->song.launcher.scenes[0].name == "Verse" &&
                legacy->song.launcher.scenes[1].name.empty() &&
                legacy->song.launcher.quantization == LaunchQuantization::bar,
            "its names read as they were written");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: blokkily_scene_launcher_tests <case>\n";
        return 2;
    }
    const std::string name = argv[1];
    try {
        if (name == "quantized_launch") quantized_launch_case();
        else if (name == "downbeat_after_wrap") downbeat_after_wrap_case();
        else if (name == "follow_actions") follow_actions_case();
        else if (name == "stop_releases") stop_releases_case();
        else if (name == "record_prints_arrangement") record_prints_arrangement_case();
        else if (name == "edit_while_launched") edit_while_launched_case();
        else if (name == "serialization") serialization_case();
        else {
            std::cerr << "Unknown case: " << name << "\n";
            return 2;
        }
    } catch (const std::exception& ex) {
        std::cerr << "FAILED: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
