// Executable scenarios for features/dynamic_modulation.feature (phase 2, wave
// 5.1). Each case is its own CTest test, dynamic_modulation_<case>.
//
// Every audio claim is read off what the production SongEngine::process()
// rendered, or off a bounce read back from disk. The instrument is the real
// CLAP fixture loaded through the production adapter: while a key is held it
// outputs a steady level, its parameter 0 (Level, 0..1, default 0.25) plus
// the CLAP_EVENT_PARAM_MOD amount it last received for it. So a modulation
// is heard directly as the level of the bus, and the fixture also reports
// the amounts it received (blokkily_test_modulations).
//
// At 120 bpm and 480 ticks a beat, one tick is 50 samples at 48 kHz and a
// 4/4 bar is 96000 samples.

#include "support/audio_probe.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/modulation.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/dynamic_library.hpp"
#include "blokkily/project/project.hpp"


#include <algorithm>
#include <array>
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
constexpr std::size_t block = 256;
constexpr Tick bar = 1920;
constexpr std::int32_t level_parameter = 0;
// A centred strip's constant-power pan: what every track reaches the master
// at.
constexpr double centre = 0.70710678118654752;

bool near(double value, double expected, double tolerance = 1e-4) {
    return std::abs(value - expected) <= tolerance;
}

// One track per entry, each playing its pattern once from the top. A pattern
// holds its key from `from` to `to` ticks, struck again at `split` when that
// lies between them (a note-off and a note-on on one sample).
Pattern held(Tick from, Tick to, Tick split = -1) {
    Pattern pattern(bar, 480);
    const auto add = [&pattern](Tick start, Tick end) {
        Trigger trigger;
        trigger.start = start;
        trigger.duration = end - start;
        trigger.musical_data = Note{60, 1.0F, 0.0F};
        (void)pattern.add(trigger);
    };
    if (split > from && split < to) {
        add(from, split);
        add(split, to);
    } else {
        add(from, to);
    }
    return pattern;
}

// A key struck every 24 ticks (1200 samples) all bar long: a recompile lets
// go of what the arrangement held, and the next strike sounds it again.
Pattern restruck() {
    Pattern pattern(bar, 480);
    for (Tick start = 0; start < bar; start += 24) {
        Trigger trigger;
        trigger.start = start;
        trigger.duration = 24;
        trigger.musical_data = Note{60, 1.0F, 0.0F};
        (void)pattern.add(trigger);
    }
    return pattern;
}

Song song_of(std::vector<Pattern> patterns) {
    Song song;
    song.patterns.clear();
    song.tracks.clear();
    song.clips.clear();
    for (std::size_t index = 0; index < patterns.size(); ++index) {
        song.patterns.push_back({"P" + std::to_string(index), std::move(patterns[index])});
        Track track;
        track.name = "Track " + std::to_string(index);
        track.instrument = {"CLAP", BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", {}};
        song.tracks.push_back(track);
        song.clips.push_back({index, index, 0, 1});
    }
    return song;
}

std::unique_ptr<PluginInstance> clap_instrument() {
    std::string error;
    auto instance = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
    require(instance != nullptr, "the CLAP fixture must load through the production adapter: " + error);
    return instance;
}

std::unique_ptr<PluginInstance> clap_effect() {
    std::string error;
    auto instance = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_EFFECT_PATH,
                                               "dev.blokkily.test.effect", &error);
    require(instance != nullptr, "the CLAP effect fixture must load: " + error);
    return instance;
}

void prepare(SongEngine& engine, const Song& song) {
    for (std::size_t track = 0; track < song.tracks.size(); ++track)
        engine.set_instrument(track, clap_instrument());
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
}

struct Stereo {
    std::vector<float> left;
    std::vector<float> right;
};

Stereo render(SongEngine& engine, std::size_t frames) {
    Stereo out;
    out.left.resize(frames);
    out.right.resize(frames);
    for (std::size_t done = 0; done < frames; done += block) {
        const auto now = std::min(block, frames - done);
        engine.process({std::span(out.left).subspan(done, now),
                        std::span(out.right).subspan(done, now)});
    }
    return out;
}

// The steady level a stretch of the master sits at (its mean), as the
// track's own level: the centred strip's pan taken back out.
double level_over(const std::vector<float>& samples, std::size_t from, std::size_t to) {
    double sum = 0.0;
    for (std::size_t index = from; index < to; ++index) sum += samples[index];
    return sum / static_cast<double>(to - from) / centre;
}

// What the fixture says it received: the last modulation of each parameter
// and how many modulation events arrived.
struct Received {
    std::array<double, 3> amounts{};
    long count = 0;
};
Received received() {
    void* library = blokkily::dynamic_library::open_loaded(BLOKKILY_TEST_CLAP_PATH);
    require(library != nullptr, "the CLAP fixture must already be loaded");
    auto* report = reinterpret_cast<long (*)(double*)>(
        blokkily::dynamic_library::symbol(library, "blokkily_test_modulations"));
    require(report != nullptr, "the fixture reports the modulations it received");
    Received result;
    result.count = report(result.amounts.data());
    blokkily::dynamic_library::close(library);
    return result;
}

Modulator lfo(LfoShape shape, double rate_hz, ProcessorAddress where, double depth) {
    Modulator modulator;
    modulator.kind = Modulator::Kind::lfo;
    modulator.name = "LFO";
    modulator.shape = shape;
    modulator.rate_hz = rate_hz;
    modulator.targets = {{where, level_parameter, depth}};
    return modulator;
}

Modulator macro(double value, ProcessorAddress where, double depth,
                std::int32_t parameter = level_parameter) {
    Modulator modulator;
    modulator.kind = Modulator::Kind::macro;
    modulator.name = "Macro";
    modulator.value = value;
    modulator.targets = {{where, parameter, depth}};
    return modulator;
}

// --- Scenarios ------------------------------------------------------------

// features/dynamic_modulation.feature: LFO shapes.
void lfo_shapes_case() {
    require(near(lfo_value(LfoShape::sine, 0.25), 1.0) &&
                near(lfo_value(LfoShape::sine, 0.75), -1.0) &&
                near(lfo_value(LfoShape::sine, 0.0), 0.0),
            "the sine peaks at a quarter cycle and dips at three quarters");
    require(near(lfo_value(LfoShape::triangle, 0.25), 1.0) &&
                near(lfo_value(LfoShape::triangle, 0.5), 0.0) &&
                near(lfo_value(LfoShape::triangle, 0.75), -1.0),
            "the triangle turns at a quarter and three quarters");
    require(near(lfo_value(LfoShape::saw_up, 0.0), -1.0) &&
                near(lfo_value(LfoShape::saw_up, 0.5), 0.0) &&
                near(lfo_value(LfoShape::saw_down, 0.0), 1.0),
            "the saws start at their extremes");
    require(lfo_value(LfoShape::square, 0.1) == 1.0F && lfo_value(LfoShape::square, 0.6) == -1.0F &&
                lfo_value(LfoShape::square, 1.1) == 1.0F,
            "the square is high for the first half of every cycle");
    // Random: one value per cycle, the same one every time it is asked.
    const float held = lfo_value(LfoShape::sample_and_hold, 3.1);
    require(held == lfo_value(LfoShape::sample_and_hold, 3.9) &&
                held == lfo_value(LfoShape::sample_and_hold, 3.1) && held >= -1.0F &&
                held <= 1.0F,
            "sample and hold keeps one value through a cycle");
    bool moves = false;
    for (int cycle = 0; cycle < 8; ++cycle)
        moves = moves || lfo_value(LfoShape::sample_and_hold, cycle + 0.5) != held;
    require(moves, "sample and hold takes a new value in another cycle");
}

// features/dynamic_modulation.feature: An envelope follower tracks a level.
void envelope_follower_case() {
    EnvelopeFollower follower;
    std::vector<float> silent(block, 0.0F);
    std::vector<float> loud(block, 0.5F);
    require(follower.follow({silent, silent}, rate, 5.0, 50.0) == 0.0F, "silence is 0");
    float rising = 0.0F;
    for (int step = 0; step < 20; ++step) rising = follower.follow({loud, loud}, rate, 5.0, 50.0);
    require(near(rising, 0.5, 0.01), "it rises to the level, got " + text(rising));
    const float after_one = follower.follow({silent, silent}, rate, 5.0, 50.0);
    require(after_one < rising && after_one > 0.4F, "it falls with the release, not at once");
    float falling = after_one;
    for (int step = 0; step < 60; ++step) falling = follower.follow({silent, silent}, rate, 5.0, 50.0);
    require(falling < 0.02F, "it falls to silence, got " + text(falling));
}

// features/dynamic_modulation.feature: An LFO moves a CLAP instrument's
// parameter, heard in the rendered audio. Also the regression for event
// order: a modulation must reach the plugin in time order, ahead of the notes
// later in the same block, or the samples before the first note hear the
// previous modulation.
void lfo_reaches_clap_case() {
    // Key held all bar long, struck again at tick 482 (sample 24100): inside
    // the block [24064, 24320), the first one past the LFO's half cycle.
    auto song = song_of({held(0, bar - 1, 482)});
    // A 1 Hz square, 0.2 of Level's range (0..1): +0.2 for the first half
    // second, -0.2 for the next.
    song.modulators = {lfo(LfoShape::square, 1.0, track_instrument(0), 0.2)};
    require(song.consistent(), "the song is valid");
    SongEngine engine;
    prepare(engine, song);
    const auto before = received().count;
    engine.set_playing(true);
    const auto out = render(engine, 48000);
    const auto got = received();
    require(got.count - before >= 48000 / static_cast<long>(block),
            "the fixture receives a modulation every block, got " +
                std::to_string(got.count - before));
    require(near(got.amounts[0], -0.2, 1e-6),
            "the last amount it received is -0.2 of Level, got " + text(got.amounts[0]));
    const double high = level_over(out.left, 1000, 23000);
    const double low = level_over(out.left, 24400, 47000);
    require(near(high, 0.45, 1e-4) && near(low, 0.05, 1e-4),
            "the level swings 0.25 +/- 0.2: high " + text(high) + ", low " + text(low));
    // The block where the LFO turned down also holds a note-off and a
    // note-on at sample 24100: every sample of it hears the new modulation.
    const double before_strike = level_over(out.left, 24064, 24100);
    require(near(before_strike, 0.05, 1e-4),
            "samples before the re-strike hear this block's modulation, got " +
                text(before_strike));
    std::cerr << "lfo: high " << high << " low " << low << " pre-strike " << before_strike << '\n';
}

// features/dynamic_modulation.feature: A synced LFO follows the tempo map.
// A square LFO locked to one beat, aimed at the CLAP synth's Level, over a
// song at 120 BPM for its first two bars and 60 BPM after: the level turns
// every half beat, 12000 samples apart at 120 and 24000 at 60, whatever the
// LFO's free rate says. A tempo edit recompiled into the running engine moves
// it too. Heard in the rendered audio and in the modulation events the
// fixture received.
void lfo_tempo_sync_case() {
    constexpr Tick bars = 4;
    Pattern pattern(bar * bars, 480);
    Trigger trigger;
    trigger.start = 0;
    trigger.duration = bar * bars - 1;
    trigger.musical_data = Note{60, 1.0F, 0.0F};
    (void)pattern.add(trigger);
    auto song = song_of({std::move(pattern)});
    song.tempo.points = {{0, 120.0, false}, {2 * bar, 60.0, false}};
    auto synced = lfo(LfoShape::square, 7.0, track_instrument(0), 0.2);
    synced.sync_beats = 1.0;
    song.modulators = {synced};
    std::string why;
    require(song.consistent(&why), "the song is valid: " + why);
    SongEngine engine;
    prepare(engine, song);
    engine.set_playing(true);
    // 8 beats at 24000 samples and 8 at 48000.
    const std::size_t at_120 = 8 * 24000;
    const std::size_t total = at_120 + 8 * 48000;
    const auto out = render(engine, total);

    // Where the level turns (between 0.45 and 0.05 of the strip).
    const auto turns = [&](const std::vector<float>& samples, std::size_t from, std::size_t to) {
        std::vector<std::size_t> found;
        bool high = samples[from] / centre > 0.25;
        for (std::size_t index = from + 1; index < to; ++index) {
            const bool now = samples[index] / centre > 0.25;
            if (now != high) found.push_back(index);
            high = now;
        }
        return found;
    };
    const auto spaced = [&](const std::vector<std::size_t>& found, double spacing) {
        if (found.size() < 4) return false;
        for (std::size_t index = 1; index < found.size(); ++index) {
            const double gap = static_cast<double>(found[index] - found[index - 1]);
            // Evaluated once per block: a turn lands on the block after it.
            if (std::abs(gap - spacing) > static_cast<double>(block)) return false;
        }
        return true;
    };
    const auto fast = turns(out.left, 1000, at_120 - 1000);
    const auto slow = turns(out.left, at_120 + 1000, total - 1000);
    require(spaced(fast, 12000.0),
            "at 120 BPM the one-beat square turns every 12000 samples (" +
                std::to_string(fast.size()) + " turns)");
    require(spaced(slow, 24000.0),
            "at 60 BPM it turns every 24000 samples (" + std::to_string(slow.size()) + " turns)");
    // Its free rate (7 Hz) would turn every ~3400 samples: it is not used.
    require(near(level_over(out.left, 1000, 11000), 0.45, 1e-4) &&
                near(level_over(out.left, 13000, 23000), 0.05, 1e-4),
            "high for the first half beat, low for the second");
    const auto amount = received().amounts[0];
    require(near(std::abs(amount), 0.2, 1e-6), "the fixture received +/-0.2 of Level, got " +
                                                   text(amount));

    // The tempo edited to 240 BPM while the song plays: recompiled, the same
    // LFO turns every 6000 samples.
    song.tempo.points = {{0, 240.0, false}};
    require(engine.recompile(song, 0, &why), "the tempo edit recompiles: " + why);
    engine.seek(0);
    const auto edited = render(engine, 8 * 12000);
    const auto faster = turns(edited.left, 1000, 8 * 12000 - 1000);
    require(spaced(faster, 6000.0),
            "at 240 BPM it turns every 6000 samples (" + std::to_string(faster.size()) + " turns)");
    std::cerr << "lfo sync: " << fast.size() << " turns at 120, " << slow.size() << " at 60, "
              << faster.size() << " at 240\n";
}

// features/dynamic_modulation.feature: A macro moves every parameter it
// targets, scaled to each parameter's range; turning it reaches the running
// engine without a recompile; modulators on one parameter add up.
void macro_live_case() {
    auto song = song_of({held(0, bar - 1)});
    Track& track = song.tracks[0];
    EffectSlot effect;
    effect.plugin = {"CLAP", BLOKKILY_TEST_CLAP_EFFECT_PATH, "dev.blokkily.test.effect", {}};
    track.inserts = {effect};
    const ProcessorAddress insert{BusKind::track, 0, 0};
    // Macro A: 0.4 of Level's range at full; macro B: -0.2 of it, and half
    // of the effect's Gain (0..1) at full.
    song.modulators = {macro(0.5, track_instrument(0), 0.4), macro(1.0, track_instrument(0), -0.2)};
    song.modulators[1].targets.push_back({insert, 0, 0.5});
    require(song.consistent(), "the song is valid");
    SongEngine engine;
    engine.set_processor(insert, clap_effect());
    prepare(engine, song);
    engine.set_playing(true);
    // Level 0.25 + 0.5 x 0.4 - 0.2 = 0.25; effect gain 0.25 + 0.5 = 0.75.
    auto out = render(engine, 9600);
    const double first = level_over(out.left, 4800, 9600);
    require(near(first, 0.25 * 0.75, 1e-4), "0.25 through a gain of 0.75, got " + text(first));
    const auto got = received();
    require(near(got.amounts[0], 0.0, 1e-6), "Level receives one summed amount, 0.2 - 0.2");

    // Turned while playing: macro A to 1 adds 0.4 - 0.2 = +0.2 to Level.
    song.modulators[0].value = 1.0;
    engine.apply_mix(song);
    out = render(engine, 4800);
    const double turned = level_over(out.left, 256, 4800);
    require(near(turned, 0.45 * 0.75, 1e-4), "the macro is heard at once, got " + text(turned));
    require(near(received().amounts[0], 0.2, 1e-6), "Level now receives +0.2");
    // A depth moves the same way: macro B's Gain depth to 0 leaves the
    // effect at its own 0.25.
    song.modulators[1].targets[1].depth = 0.0;
    engine.apply_mix(song);
    out = render(engine, 4800);
    const double undepth = level_over(out.left, 256, 4800);
    require(near(undepth, 0.45 * 0.25, 1e-4), "the depth is heard at once, got " + text(undepth));
    // Nothing let the note go: the level never dipped while the knobs turned.
    require(probe::peak(std::span<const float>(out.left).first(256)) > 0.05F,
            "the key kept sounding across the moves");
}

// features/dynamic_modulation.feature: A follower follows its source track's
// audio, after its inserts and before its fader, and its source renders first.
void follower_hears_source_case() {
    // Track 0 holds its key all bar; track 1 (the source) only for the first
    // half. Track 1 is muted and faded right down: the follower still hears
    // it, and nothing of it reaches the master.
    auto song = song_of({held(0, bar - 1), held(0, bar / 2)});
    song.tracks[1].mix.mute = true;
    song.tracks[1].mix.gain_db = -60.0;
    Modulator follower;
    follower.kind = Modulator::Kind::follower;
    follower.name = "Follow";
    follower.source_track = 1;
    follower.attack_ms = 2.0;
    follower.release_ms = 20.0;
    follower.targets = {{track_instrument(0), level_parameter, -0.4}};
    song.modulators = {follower};
    require(song.consistent(), "the song is valid");
    SongEngine engine;
    prepare(engine, song);
    engine.set_playing(true);
    const auto out = render(engine, 96000);
    // While the source plays at 0.25: 0.25 - 0.4 x 0.25 = 0.15.
    const double ducked = level_over(out.left, 20000, 47000);
    // Well after it stops, the level is back.
    const double back = level_over(out.left, 80000, 95000);
    require(near(ducked, 0.15, 1e-3), "the follower ducks track 0 to 0.15, got " + text(ducked));
    require(near(back, 0.25, 1e-3), "and lets go after the source stops, got " + text(back));
    std::cerr << "follower: ducked " << ducked << " back " << back << '\n';
}

// features/dynamic_modulation.feature: An export has the modulation that
// playback had.
void bounce_matches_playback_case() {
    auto song = song_of({held(0, bar - 1), held(0, bar / 2)});
    song.modulators = {lfo(LfoShape::sine, 3.0, track_instrument(0), 0.1)};
    Modulator follower;
    follower.kind = Modulator::Kind::follower;
    follower.source_track = 1;
    follower.targets = {{track_instrument(0), level_parameter, -0.2}};
    song.modulators.push_back(follower);
    song.modulators.push_back(macro(0.5, track_instrument(1), 0.3));
    require(song.consistent(), "the song is valid");
    SongEngine engine;
    prepare(engine, song);
    // Played for a while first: the export must not depend on it.
    engine.set_playing(true);
    (void)render(engine, 30000);
    engine.set_playing(false);
    std::filesystem::create_directories(BLOKKILY_TEST_ARTIFACTS);
    const auto file = std::filesystem::path(BLOKKILY_TEST_ARTIFACTS) / "dynamic_modulation.wav";
    std::string error;
    const auto report = bounce_song(engine, file, WaveFormat::float32, 0, &error);
    require(report.has_value(), "the bounce is written: " + error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value() && wave->channels == 2 && wave->frames == engine.song_samples(),
            "the bounce reads back, the song's length: " + error);
    engine.seek(0);
    engine.set_playing(true);
    const auto live = render(engine, static_cast<std::size_t>(wave->frames));
    engine.set_playing(false);
    double worst = 0.0;
    double spread = 0.0;
    for (std::size_t frame = 0; frame < wave->frames; ++frame) {
        worst = std::max({worst,
                          std::abs(static_cast<double>(wave->interleaved[2 * frame] - live.left[frame])),
                          std::abs(static_cast<double>(wave->interleaved[2 * frame + 1] -
                                                       live.right[frame]))});
        spread = std::max(spread, std::abs(static_cast<double>(live.left[frame] - live.left[0])));
    }
    require(spread > 0.05, "the modulation is audible in the render, spread " + text(spread));
    require(worst < 1e-6, "the bounce read back equals the live render, worst " + text(worst));
}

// Regression: a modulator removed while the song plays left its last offset
// on the plugin for ever. The parameter must be sent a modulation of 0.
void removed_modulation_releases_case() {
    auto song = song_of({restruck()});
    song.modulators = {macro(1.0, track_instrument(0), 0.3)};
    SongEngine engine;
    prepare(engine, song);
    engine.set_playing(true);
    auto out = render(engine, 4800);
    require(near(level_over(out.left, 1000, 4800), 0.55, 1e-4), "the macro adds 0.3");
    song.modulators.clear();
    std::string error;
    require(engine.recompile(song, 0, &error), "recompile: " + error);
    out = render(engine, 9600);
    const double after = level_over(out.left, 4800, 9600);
    require(near(after, 0.25, 1e-4), "the offset goes with the modulator, got " + text(after));
}

// features/dynamic_modulation.feature: modulators live in the song: saved,
// validated, and kept pointing at the right processors.
void song_model_case() {
    auto song = song_of({held(0, bar - 1), held(0, bar - 1)});
    EffectSlot compressor;
    compressor.plugin = {"Built-in", "", "compressor", {}};
    compressor.sidechain = 0;
    EffectSlot eq;
    eq.plugin = {"Built-in", "", "eq3", {}};
    song.tracks[1].inserts = {eq, compressor};
    song.modulators = {lfo(LfoShape::triangle, 2.5, track_instrument(0), -0.25),
                       macro(0.75, {BusKind::track, 1, 1}, 0.5, 1)};
    song.modulators[0].sync_beats = 0.5;
    song.modulators[0].name = "Wobble one";
    Modulator follower;
    follower.kind = Modulator::Kind::follower;
    follower.source_track = 0;
    follower.attack_ms = 3.5;
    follower.release_ms = 250.0;
    follower.targets = {{{BusKind::track, 1, 0}, 2, 0.125}};
    song.modulators.push_back(follower);
    require(song.consistent(), "the song is valid");

    // Saved and read back exactly.
    Project project;
    project.song = song;
    std::string error;
    const auto text_form = ProjectFile::serialize(project);
    const auto back = ProjectFile::parse(text_form, &error);
    require(back.has_value(), "the project reads back: " + error);
    require(back->song.modulators == song.modulators, "modulators round-trip");
    require(back->song.tracks[1].inserts == song.tracks[1].inserts, "the key round-trips");
    require(ProjectFile::serialize(*back) == text_form, "the file is the same bytes again");
    // A project saved before modulation existed loads with none.
    std::string old_form;
    std::istringstream lines(text_form);
    for (std::string line; std::getline(lines, line);)
        if (line.rfind("modulator ", 0) != 0 && line.rfind("modtarget ", 0) != 0 &&
            line.rfind("sidechain ", 0) != 0)
            old_form += line + '\n';
    const auto old = ProjectFile::parse(old_form, &error);
    require(old.has_value() && old->song.modulators.empty() &&
                !old->song.tracks[1].inserts[1].sidechain,
            "an older project loads without modulation: " + error);

    // The schema refuses what the engine could not play.
    const auto refused = [&](const std::function<void(Song&)>& change, const std::string& what) {
        auto broken = song;
        change(broken);
        std::string why;
        require(!broken.consistent(&why), what + " must be refused");
        Project saved;
        saved.song = broken;
        require(!ProjectFile::parse(ProjectFile::serialize(saved), &why).has_value(),
                what + " must not load either");
    };
    refused([](Song& s) { s.modulators[0].targets[0].processor = {BusKind::track, 5, -1}; },
            "a target on a track that does not exist");
    refused([](Song& s) { s.modulators[1].targets[0].processor = {BusKind::track, 1, 7}; },
            "a target on an insert that does not exist");
    refused([](Song& s) { s.modulators[0].targets[0].depth = 1.5; }, "a depth beyond 1");
    refused([](Song& s) { s.modulators[0].rate_hz = 0.0; }, "an LFO that never moves");
    refused([](Song& s) { s.modulators[2].source_track = 9; }, "a follower of nothing");
    refused([](Song& s) { s.modulators[0].targets.push_back(s.modulators[0].targets[0]); },
            "one parameter targeted twice by one modulator");
    refused([](Song& s) { s.tracks[1].inserts[1].sidechain = 1; }, "a track keying itself");
    refused([](Song& s) { s.tracks[1].inserts[1].sidechain = 4; }, "a key from nowhere");
    refused(
        [](Song& s) {
            EffectSlot keyed;
            keyed.plugin = {"Built-in", "", "compressor", {}};
            keyed.sidechain = 1;
            s.tracks[0].inserts = {keyed};
        },
        "two tracks keying each other");

    // Removing an insert takes its targets and moves the later ones down.
    auto edited = song;
    require(edited.remove_insert(BusKind::track, 1, 0), "the EQ is removed");
    require(edited.modulators[1].targets[0].processor == ProcessorAddress{BusKind::track, 1, 0} &&
                edited.modulators[2].targets.empty() && edited.tracks[1].inserts[0].sidechain == 0u,
            "the compressor's target and key follow it to slot 0; the EQ's target goes");
    require(edited.consistent(), "and the song stays valid");
    // Removing the key's source track lets go of the key, the follower and
    // its targets; the rest follow their tracks.
    edited = song;
    require(!edited.remove_track(0).empty(), "track 0 is removed");
    require(!edited.tracks[0].inserts[1].sidechain && edited.modulators.size() == 2 &&
                edited.modulators[0].targets.empty() &&
                edited.modulators[1].targets[0].processor == ProcessorAddress{BusKind::track, 0, 1},
            "the key and follower go, the macro follows track 1 to track 0");
    require(edited.consistent(), "and the song stays valid");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: blokkily_dynamic_modulation_tests <case>\n";
        return 2;
    }
    const std::string name = argv[1];
    try {
        if (name == "lfo_shapes") lfo_shapes_case();
        else if (name == "envelope_follower") envelope_follower_case();
        else if (name == "lfo_reaches_clap") lfo_reaches_clap_case();
        else if (name == "lfo_tempo_sync") lfo_tempo_sync_case();
        else if (name == "macro_live") macro_live_case();
        else if (name == "follower_hears_source") follower_hears_source_case();
        else if (name == "bounce_matches_playback") bounce_matches_playback_case();
        else if (name == "removed_modulation_releases") removed_modulation_releases_case();
        else if (name == "song_model") song_model_case();
        else {
            std::cerr << "Unknown case: " << name << "\n";
            return 2;
        }
    } catch (const std::exception& ex) {
        std::cerr << "FAILED: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "PASS " << name << '\n';
    return 0;
}
