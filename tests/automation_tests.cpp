// Executable scenarios for features/automation.feature (plan item 3.1, record
// stage B). Each case is its own CTest test, automation_<case>.
//
// Every audio claim is read off what the production SongEngine::process()
// rendered, or off a bounce read back from disk and compared with that
// render. The instrument is the real CLAP fixture through the production
// adapter: while a key is held it outputs a steady level (0.25 by default,
// its parameter 0), so the rendered level is the level times the strip gain,
// and a gain lane is heard as the level of the bus. Inserts are the CLAP
// effect fixture, whose output is its input, 64 samples late, times its gain
// parameter (id 0).
//
// At 120 bpm and 480 ticks a beat, one tick is 50 samples at 48 kHz and a
// 4/4 bar is 96000 samples.

#include "support/audio_probe.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/project/project.hpp"
#include "blokkily/sequencer/automation_take.hpp"

#include <dlfcn.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

std::string text(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

constexpr double rate = 48000.0;
constexpr std::size_t block = 512;
constexpr Tick bar = 1920;
constexpr std::uint64_t bar_samples = 96000;
constexpr double level = 0.25;
constexpr double centre = 0.70710678118654752; // constant-power pan, centred

bool near(double value, double expected, double tolerance) {
    return std::abs(value - expected) <= tolerance;
}

// A key struck every `restrike` ticks, each held until the next, on every
// track from the first tick to the last, so the CLAP fixture sounds its level
// throughout. A seek or a recompile lets go of what the arrangement held; the
// next strike, at most `restrike` ticks (1200 samples) on, sounds it again.
constexpr Tick restrike = 24;
Song held_song(std::size_t tracks, Tick length = 2 * bar) {
    Pattern pattern(length, 480);
    for (Tick start = 0; start < length; start += restrike) {
        Trigger trigger;
        trigger.start = start;
        trigger.duration = std::min(restrike, length - start);
        trigger.musical_data = Note{60, 1.0F, 0.0F};
        (void)pattern.add(trigger);
    }
    Song song;
    song.patterns = {{"Hold", std::move(pattern)}};
    song.tracks.assign(tracks, Track{});
    song.clips.clear();
    for (std::size_t track = 0; track < tracks; ++track) {
        song.tracks[track].name = "Track " + std::to_string(track);
        song.clips.push_back({track, 0, 0, 1});
    }
    return song;
}

AutomationTarget strip(AutomationTarget::Kind kind, std::uint32_t track = 0) {
    AutomationTarget target;
    target.kind = kind;
    target.processor = track_instrument(track);
    return target;
}

AutomationTarget parameter(ProcessorAddress where, std::int32_t index) {
    AutomationTarget target;
    target.kind = AutomationTarget::Kind::parameter;
    target.parameter_index = index;
    target.processor = where;
    return target;
}

EffectSlot clap_effect() {
    EffectSlot slot;
    slot.plugin = {"CLAP", BLOKKILY_TEST_CLAP_EFFECT_PATH, "dev.blokkily.test.effect", {}};
    return slot;
}

std::unique_ptr<PluginInstance> clap_instrument() {
    std::string error;
    auto instance = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
    require(instance != nullptr, "the CLAP fixture must load through the production adapter: " + error);
    return instance;
}

std::unique_ptr<PluginInstance> clap_effect_instance() {
    std::string error;
    auto instance = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_EFFECT_PATH,
                                               "dev.blokkily.test.effect", &error);
    require(instance != nullptr, "the CLAP effect fixture must load: " + error);
    return instance;
}

struct Stereo {
    std::vector<float> left;
    std::vector<float> right;
};

// `frames` frames of the production callback, `block` at a time, with
// `between(n)` called before the n-th block.
Stereo render(SongEngine& engine, std::size_t frames,
              const std::function<void(std::size_t)>& between = {}) {
    Stereo out;
    out.left.resize(frames);
    out.right.resize(frames);
    for (std::size_t done = 0, index = 0; done < frames; done += block, ++index) {
        if (between) between(index);
        const auto now = std::min(block, frames - done);
        engine.process({std::span(out.left).subspan(done, now),
                        std::span(out.right).subspan(done, now)});
    }
    return out;
}

double rms(const std::vector<float>& samples, std::size_t from, std::size_t to) {
    return probe::rms(std::span<const float>(samples).subspan(from, to - from));
}

void prepare(SongEngine& engine, const Song& song) {
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
}

void recompile(SongEngine& engine, const Song& song) {
    std::string error;
    require(engine.recompile(song, 0, &error), "recompile: " + error);
}

std::filesystem::path artifact(const std::string& name) {
    std::filesystem::create_directories(BLOKKILY_TEST_ARTIFACTS);
    return std::filesystem::path(BLOKKILY_TEST_ARTIFACTS) / name;
}

// Bounces the engine's song, reads the file back, and checks it is what the
// same engine renders live from the top. Returns the file's left and right.
Stereo bounce_and_compare(SongEngine& engine, const std::string& name) {
    const auto file = artifact(name);
    std::string error;
    const auto report = bounce_song(engine, file, WaveFormat::float32, 0, &error);
    require(report.has_value(), "the bounce is written: " + error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value() && wave->channels == 2, "the bounce reads back: " + error);
    require(wave->frames == engine.song_samples(), "the bounce is the song's length");
    Stereo read;
    for (std::size_t frame = 0; frame < wave->frames; ++frame) {
        read.left.push_back(wave->interleaved[frame * 2]);
        read.right.push_back(wave->interleaved[frame * 2 + 1]);
    }
    // The export is the mix that was auditioned: the engine played live from
    // the top renders the very same samples.
    engine.seek(0);
    engine.set_playing(true);
    const auto live = render(engine, static_cast<std::size_t>(wave->frames));
    engine.set_playing(false);
    double worst = 0.0;
    for (std::size_t frame = 0; frame < read.left.size(); ++frame)
        worst = std::max({worst, std::abs(static_cast<double>(read.left[frame] - live.left[frame])),
                          std::abs(static_cast<double>(read.right[frame] - live.right[frame]))});
    require(worst < 1e-6, "the bounce read back equals the live render, worst " + text(worst));
    return read;
}

template <typename Function>
Function fixture_hook(const char* binary, const char* name) {
    void* library = dlopen(binary, RTLD_NOW | RTLD_NOLOAD);
    require(library != nullptr, std::string("the fixture must already be loaded: ") + binary);
    auto* function = reinterpret_cast<Function>(dlsym(library, name));
    dlclose(library);
    require(function != nullptr, std::string("the fixture must export ") + name);
    return function;
}

// Scenario: A gain lane is heard, and bounced, as a ramp.
void gain_ramp_bounce() {
    auto song = held_song(1);
    song.tracks[0].automation.push_back(
        {strip(AutomationTarget::Kind::gain), {{0, -40.0}, {2 * bar, 0.0}}});
    SongEngine engine;
    engine.set_instrument(0, clap_instrument());
    prepare(engine, song);
    const auto bounced = bounce_and_compare(engine, "automation-gain-ramp.wav");

    // Eight quarter notes, each louder than the one before by the 5 dB the
    // lane climbs across it, and each where the lane says it is.
    constexpr std::size_t quarter = 24000;
    double previous = 0.0;
    std::ostringstream levels;
    for (std::size_t index = 0; index < 8; ++index) {
        const double heard = rms(bounced.left, index * quarter, (index + 1) * quarter);
        const double lane_db = -40.0 + 5.0 * (static_cast<double>(index) + 0.5);
        const double expected = level * centre * std::pow(10.0, lane_db / 20.0);
        levels << ' ' << heard;
        require(heard > previous * 1.5, "quarter " + std::to_string(index) +
                                            " is louder than the one before:" + levels.str());
        require(std::abs(20.0 * std::log10(heard / expected)) < 0.5,
                "quarter " + std::to_string(index) + " sounds at the lane's level:" +
                    levels.str() + " want " + text(expected));
        previous = heard;
    }
    std::cout << "per-quarter RMS:" << levels.str() << '\n';
}

// Scenario: Pan and mute lanes move the track around and off the bus.
void pan_and_mute_lanes() {
    auto song = held_song(1);
    song.tracks[0].mix.pan = 0.3; // what the lanes replace
    song.tracks[0].automation.push_back(
        {strip(AutomationTarget::Kind::pan), {{0, -1.0}, {bar, -1.0}, {bar, 1.0}}});
    song.tracks[0].automation.push_back(
        {strip(AutomationTarget::Kind::mute), {{0, 0.0}, {bar + bar / 2, 0.0}, {bar + bar / 2, 1.0}}});
    SongEngine engine;
    engine.set_instrument(0, clap_instrument());
    prepare(engine, song);
    engine.set_playing(true);
    auto heard = render(engine, 2 * bar_samples - block);
    const auto window = [&](std::size_t from, std::size_t to) {
        return std::pair{rms(heard.left, from, to), rms(heard.right, from, to)};
    };
    const auto [left_a, right_a] = window(10000, 86000);
    const auto [left_b, right_b] = window(106000, 134000);
    const auto [left_c, right_c] = window(154000, 186000);
    require(near(left_a, level, 1e-4) && right_a < 1e-6,
            "bar 1 is hard left: " + text(left_a) + '/' + text(right_a));
    require(left_b < 1e-6 && near(right_b, level, 1e-4),
            "bar 2 is hard right: " + text(left_b) + '/' + text(right_b));
    require(left_c < 1e-6 && right_c < 1e-6,
            "the second half of bar 2 is muted: " + text(left_c) + '/' + text(right_c));

    // Off: the lanes are ignored and the strip plays as the mixer has it.
    song.tracks[0].automation_mode = AutomationMode::off;
    recompile(engine, song);
    engine.seek(0);
    heard = render(engine, 2 * bar_samples - block);
    const double angle = (0.3 + 1.0) * 0.25 * 3.14159265358979323846;
    const auto [left_off, right_off] = window(154000, 186000);
    require(near(left_off, level * std::cos(angle), 1e-4) &&
                near(right_off, level * std::sin(angle), 1e-4),
            "with automation off the static pan plays and nothing mutes: " + text(left_off) +
                '/' + text(right_off));
}

// Scenario: Holding a fader in touch mode overrides its lane, without a
// recompile.
void touch_override() {
    auto song = held_song(1);
    song.tracks[0].automation_mode = AutomationMode::touch;
    song.tracks[0].automation.push_back({strip(AutomationTarget::Kind::gain), {{0, -20.0}}});
    SongEngine engine;
    engine.set_instrument(0, clap_instrument());
    prepare(engine, song);
    engine.set_playing(true);
    const double lane = level * centre * 0.1;
    const double unity = level * centre;
    const auto next_block = [&] {
        const auto heard = render(engine, block);
        return static_cast<double>(heard.left[block / 2]);
    };
    // Past the next strike after a recompile or a restart let go of the key.
    const auto restruck = [&] { (void)render(engine, 3 * block); };
    (void)next_block();
    require(near(next_block(), lane, 1e-5), "the lane plays at -20 dB");

    // Taken hold of at 0 dB: the live fader wins from the very next block.
    const auto touched_at = engine.sample_position();
    require(engine.move({0, StripControl::gain, 0.0, true}), "the move is queued");
    require(near(next_block(), unity, 1e-5), "the held fader overrides the lane");
    require(near(next_block(), unity, 1e-5), "and keeps doing so while held");
    const auto released_at = engine.sample_position();
    require(engine.move({0, StripControl::gain, 0.0, false}), "the release is queued");
    require(near(next_block(), lane, 1e-5), "let go, the strip returns to its lane");

    // Both moves came back stamped with the block they sounded in.
    StripMoveEvent played;
    require(engine.take_strip_move(played) && played.move.touching &&
                played.song_sample == touched_at &&
                played.tick == tick_at_sample(engine.published_clock(), touched_at),
            "the touch is stamped with its block's sample and tick");
    require(engine.take_strip_move(played) && !played.move.touching &&
                played.song_sample == released_at,
            "the release is stamped with its block's sample");
    require(!engine.take_strip_move(played), "nothing else was played");

    // Read: the lane wins even while the fader is held.
    song.tracks[0].automation_mode = AutomationMode::read;
    recompile(engine, song);
    restruck();
    require(engine.move({0, StripControl::gain, 0.0, true}), "held in read mode");
    const double held_read = next_block();
    require(near(held_read, lane, 1e-5), "read mode plays the lane under a held fader: " + text(held_read));
    require(engine.move({0, StripControl::gain, 0.0, false}), "released");

    // Latch: from the first touch the live value stays until the stop.
    song.tracks[0].automation_mode = AutomationMode::latch;
    recompile(engine, song);
    restruck();
    require(engine.move({0, StripControl::gain, -6.0, true}), "latched");
    require(engine.move({0, StripControl::gain, -6.0, false}), "and let go");
    const double latched = level * centre * std::pow(10.0, -6.0 / 20.0);
    for (int index = 0; index < 4; ++index)
        require(near(next_block(), latched, 1e-5), "latch holds the last value after release");
    engine.set_playing(false);
    (void)next_block();
    engine.set_playing(true);
    restruck();
    require(near(next_block(), lane, 1e-5), "stopping ends the latch: the lane plays again");
}

// Scenario: Recorded fader moves replay as they were heard.
void strip_moves_replay() {
    auto song = held_song(1);
    song.tracks[0].automation_mode = AutomationMode::touch;
    song.tracks[0].automation.push_back({strip(AutomationTarget::Kind::gain), {{0, 0.0}}});
    SongEngine engine;
    engine.set_instrument(0, clap_instrument());
    prepare(engine, song);
    engine.set_playing(true);

    // The fader taken hold of at 0 dB in block 3, pulled down in three steps,
    // and let go at -18 dB in block 15. The moves are three blocks (30 ticks)
    // apart, further than AutomationTake::step_gap, so each was a step.
    const std::map<std::size_t, StripMove> moves{
        {3, {0, StripControl::gain, 0.0, true}},   {6, {0, StripControl::gain, -6.0, true}},
        {9, {0, StripControl::gain, -12.0, true}}, {12, {0, StripControl::gain, -18.0, true}},
        {15, {0, StripControl::gain, -18.0, false}}};
    constexpr std::size_t blocks = 20;
    const auto live = render(engine, blocks * block, [&](std::size_t index) {
        if (const auto found = moves.find(index); found != moves.end())
            require(engine.move(found->second), "the move is queued");
    });

    // Recorded as the application records them, and written into the song.
    AutomationTake take(song.length());
    StripMoveEvent played;
    std::size_t recorded = 0;
    while (engine.take_strip_move(played)) {
        take.record(song, played);
        ++recorded;
    }
    require(recorded == moves.size(), "every move came back from the engine");
    require(take.ready(), "the release closed the pass");
    require(take.commit(song) == 1, "the pass is written into the gain lane");
    const auto& lane = song.tracks[0].automation.at(0);
    require(lane.well_formed(), "the recorded lane is well formed");

    // Played again with nobody touching anything.
    recompile(engine, song);
    engine.seek(0);
    const auto replay = render(engine, blocks * block);
    // The envelope reaches a step across the 256 samples before it, so each
    // block's first half is compared: there the two must be the same.
    double worst = 0.0;
    for (std::size_t index = 0; index < blocks; ++index)
        for (std::size_t frame = 0; frame < block / 2; ++frame) {
            const auto at = index * block + frame;
            worst = std::max(worst, std::abs(static_cast<double>(live.left[at] - replay.left[at])));
        }
    require(worst < 1e-5, "the recorded moves replay as they were heard, worst " + text(worst));
    const auto level_at = [&](std::size_t index) {
        return static_cast<double>(replay.left[index * block + 10]);
    };
    require(near(level_at(7), level * centre * std::pow(10.0, -6.0 / 20.0), 1e-5) &&
                near(level_at(13), level * centre * std::pow(10.0, -18.0 / 20.0), 1e-5) &&
                near(level_at(17), level * centre, 1e-5),
            "the replay steps down with the fader and returns to the lane after the release");
}

// Scenario: A knob turned in the plugin's own window becomes a lane.
void plugin_moves_lane() {
    auto song = held_song(1);
    song.tracks[0].automation_mode = AutomationMode::latch;
    SongEngine engine;
    engine.set_instrument(0, clap_instrument());
    prepare(engine, song);
    const auto turn = fixture_hook<void (*)(std::uint32_t, double)>(BLOKKILY_TEST_CLAP_PATH,
                                                                   "blokkily_test_gui_turn");
    engine.set_playing(true);
    AutomationTake take(song.length());
    const auto drain = [&] {
        PluginEditEvent edit;
        while (engine.take_plugin_edit(edit))
            take.record(song, edit,
                        tick_at_sample(engine.published_clock(), edit.song_sample), level);
    };
    (void)render(engine, 60 * block);
    // The level knob turned to zero: the fixture reports one gesture through
    // its out_events, which the engine stamps and hands back.
    const auto turned_at = engine.sample_position();
    turn(0, 0.0);
    const auto silent = render(engine, 100 * block);
    drain();
    require(rms(silent.left, 2 * block, silent.left.size()) < 1e-6,
            "the turn is heard: the fixture falls silent");
    const auto stopped_at = engine.sample_position();
    engine.set_playing(false);
    (void)render(engine, block);
    drain();
    take.finish(tick_at_sample(engine.published_clock(), stopped_at));
    require(take.commit(song) == 1, "the turn is written into a lane");
    require(song.tracks[0].automation.size() == 1, "one lane was made");
    const auto& lane = song.tracks[0].automation[0];
    require(lane.target.kind == AutomationTarget::Kind::parameter &&
                lane.target.parameter_index == 0 &&
                lane.target.processor == track_instrument(0),
            "the lane drives the instrument's level");
    const auto turned_tick = tick_at_sample(engine.published_clock(), turned_at);
    const auto stopped_tick = tick_at_sample(engine.published_clock(), stopped_at);
    require(near(*lane.value_at(turned_tick / 2), level, 1e-9) &&
                near(*lane.value_at(turned_tick + 10), 0.0, 1e-9) &&
                near(*lane.value_at(stopped_tick + 10), level, 1e-9),
            "the lane holds the level, drops to zero at the turn, and returns at the stop");

    // The bounce plays the lane: the level, then silence from the turn to
    // where the transport stopped, then the level again.
    std::string error;
    require(engine.recompile(song, 0, &error), "recompile: " + error);
    const auto bounced = bounce_and_compare(engine, "automation-plugin-lane.wav");
    const auto before = rms(bounced.left, 5000, turned_at - 1000);
    const auto during = rms(bounced.left, turned_at + 1000, stopped_at - 1000);
    const auto after = rms(bounced.left, stopped_at + 1000, bounced.left.size() - 1000);
    require(near(before, level * centre, 1e-4) && during < 1e-6 && near(after, level * centre, 1e-4),
            "the bounce is silenced where the knob was turned down: " + text(before) + ' ' +
                text(during) + ' ' + text(after));
}

// Scenario: A seek chases every parameter lane to where the song now is.
void chase_on_seek() {
    auto song = held_song(1);
    song.tracks[0].inserts = {clap_effect()};
    // The instrument's level and the insert's gain, each with a jump at bar 2.
    song.tracks[0].automation.push_back(
        {parameter(track_instrument(0), 0), {{0, 0.1}, {bar, 0.1}, {bar, 0.4}}});
    song.tracks[0].automation.push_back(
        {parameter({BusKind::track, 0, 0}, 0), {{0, 1.0}, {bar, 1.0}, {bar, 0.5}}});
    SongEngine engine;
    engine.set_instrument(0, clap_instrument());
    engine.set_processor({BusKind::track, 0, 0}, clap_effect_instance());
    prepare(engine, song);
    engine.set_playing(true);
    // A few blocks on from a seek, past the next strike of the key.
    const auto settled = [&] {
        const auto heard = render(engine, 8 * block);
        return rms(heard.left, 6 * block, 8 * block);
    };
    require(near(settled(), 0.1 * 1.0 * centre, 1e-4), "bar 1 plays level 0.1 at gain 1");
    engine.seek(bar_samples + bar_samples / 2);
    require(near(settled(), 0.4 * 0.5 * centre, 1e-4),
            "sought into bar 2, both lanes are chased there: level 0.4 at gain 0.5");
    // Back into bar 1, where no event lies: only a chase can bring the
    // values back.
    engine.seek(bar_samples / 4);
    require(near(settled(), 0.1 * 1.0 * centre, 1e-4), "sought back, both lanes are chased back");
    // Stopped and started again at the same place, a value changed in the
    // meantime is chased too.
    engine.set_playing(false);
    (void)render(engine, block);
    engine.seek(bar_samples + bar_samples / 2);
    (void)render(engine, block);
    engine.set_playing(true);
    require(near(settled(), 0.4 * 0.5 * centre, 1e-4), "playing from a stop chases the lanes");
}

// Scenario: Effect parameters are automatable on returns and the master.
void effect_lane_on_return() {
    auto song = held_song(1);
    song.tracks[0].mix.pan = -1.0;
    song.tracks[0].sends = {{0, 0.0, false}};
    song.returns = {ReturnBus{}};
    song.returns[0].mix.pan = 1.0;
    song.returns[0].inserts = {clap_effect()};
    song.master_inserts = {clap_effect()};
    song.tracks[0].automation.push_back(
        {parameter({BusKind::ret, 0, 0}, 0), {{0, 0.2}, {bar, 0.2}, {bar, 0.8}}});
    song.tracks[0].automation.push_back(
        {parameter({BusKind::master, 0, 0}, 0),
         {{0, 1.0}, {bar + bar / 2, 1.0}, {bar + bar / 2, 0.5}}});
    SongEngine engine;
    engine.set_instrument(0, clap_instrument());
    engine.set_processor({BusKind::ret, 0, 0}, clap_effect_instance());
    engine.set_processor({BusKind::master, 0, 0}, clap_effect_instance());
    prepare(engine, song);
    engine.set_playing(true);
    const auto heard = render(engine, 2 * bar_samples - block);
    const auto window = [&](std::size_t from, std::size_t to) {
        return std::pair{rms(heard.left, from, to), rms(heard.right, from, to)};
    };
    // Left is the track itself, right the return fed by its send.
    const auto [left_a, right_a] = window(10000, 86000);
    const auto [left_b, right_b] = window(106000, 134000);
    const auto [left_c, right_c] = window(154000, 186000);
    require(near(left_a, level, 1e-4) && near(right_a, level * 0.2, 1e-4),
            "bar 1: the return's insert at 0.2: " + text(left_a) + '/' + text(right_a));
    require(near(left_b, level, 1e-4) && near(right_b, level * 0.8, 1e-4),
            "bar 2: the return's insert at 0.8: " + text(left_b) + '/' + text(right_b));
    require(near(left_c, level * 0.5, 1e-4) && near(right_c, level * 0.8 * 0.5, 1e-4),
            "then the master's insert halves both: " + text(left_c) + '/' + text(right_c));
}

// Scenario: Lanes and modes are saved and loaded.
void round_trip() {
    auto song = held_song(2);
    song.returns = {ReturnBus{}};
    song.returns[0].inserts = {clap_effect()};
    song.master_inserts = {clap_effect()};
    song.tracks[0].inserts = {clap_effect()};
    song.tracks[0].automation_mode = AutomationMode::touch;
    song.tracks[1].automation_mode = AutomationMode::latch;
    song.tracks[0].automation = {
        {strip(AutomationTarget::Kind::gain), {{0, -12.5}, {960, 0.0}, {960, -3.0}}},
        {strip(AutomationTarget::Kind::pan), {{0, -0.25}, {3000, 0.75}}},
        {strip(AutomationTarget::Kind::mute), {{0, 0.0}, {1920, 1.0}}},
        {parameter(track_instrument(0), 0), {{120, 0.125}}},
        {parameter({BusKind::track, 0, 0}, 0), {{0, 0.5}, {240, 0.75}}},
        {parameter({BusKind::ret, 0, 0}, 0), {{0, 0.3}}},
        {parameter({BusKind::master, 0, 0}, 0), {{0, 0.9}}},
    };
    song.tracks[1].automation = {{strip(AutomationTarget::Kind::gain, 1), {{480, -6.0}}}};
    std::string why;
    require(song.consistent(&why), "the song is consistent: " + why);
    Project project;
    project.song = song;
    const auto saved = ProjectFile::serialize(project);
    std::string error;
    const auto loaded = ProjectFile::parse(saved, &error);
    require(loaded.has_value(), "the project loads: " + error);
    for (std::size_t track = 0; track < song.tracks.size(); ++track) {
        require(loaded->song.tracks[track].automation == song.tracks[track].automation,
                "track " + std::to_string(track) + "'s lanes come back as they were");
        require(loaded->song.tracks[track].automation_mode == song.tracks[track].automation_mode,
                "track " + std::to_string(track) + "'s mode comes back");
    }
    require(ProjectFile::serialize(*loaded) == saved, "saving again writes the same text");
    require(saved.find("automation 0 mute track 0 -1") != std::string::npos,
            "the mute lane is written as a mute record");

    // A mute lane outside 0..1 is refused.
    auto broken = song;
    broken.tracks[0].automation[2].points[1].value = 2.0;
    require(!broken.consistent(&why), "a mute point of 2 is refused");
}

// Scenario: Solo silences an automated track that is not soloed.
void solo_with_lane() {
    auto song = held_song(2);
    song.tracks[0].mix.pan = -1.0;
    song.tracks[1].mix.pan = 1.0;
    song.tracks[1].automation.push_back(
        {strip(AutomationTarget::Kind::gain, 1), {{0, -6.020599913}}});
    SongEngine engine;
    engine.set_instrument(0, clap_instrument());
    engine.set_instrument(1, clap_instrument());
    prepare(engine, song);
    engine.set_playing(true);
    const auto sides = [&] {
        const auto heard = render(engine, 8 * block);
        return std::pair{rms(heard.left, 6 * block, 8 * block),
                         rms(heard.right, 6 * block, 8 * block)};
    };
    auto [left, right] = sides();
    require(near(left, level, 1e-5) && near(right, level * 0.5, 1e-5),
            "unsoloed: track 1 at its lane's -6 dB: " + text(left) + '/' + text(right));
    // Solo track 0: a live mixer move, no recompile.
    song.tracks[0].mix.solo = true;
    engine.apply_mix(song);
    std::tie(left, right) = sides();
    require(near(left, level, 1e-5) && right < 1e-7,
            "track 0 soloed silences the automated track 1: " + text(left) + '/' + text(right));
    song.tracks[0].mix.solo = false;
    song.tracks[1].mix.solo = true;
    engine.apply_mix(song);
    std::tie(left, right) = sides();
    require(left < 1e-7 && near(right, level * 0.5, 1e-5),
            "track 1 soloed plays at its lane alone: " + text(left) + '/' + text(right));
}

// Scenario: A post-fader send follows the automated fader.
void post_fader_send() {
    auto song = held_song(1);
    song.tracks[0].mix.pan = -1.0;
    song.tracks[0].sends = {{0, 0.0, false}};
    song.returns = {ReturnBus{}};
    song.returns[0].mix.pan = 1.0;
    song.tracks[0].automation.push_back(
        {strip(AutomationTarget::Kind::gain), {{0, -20.0}, {bar, -20.0}, {bar, 0.0}}});
    SongEngine engine;
    engine.set_instrument(0, clap_instrument());
    prepare(engine, song);
    engine.set_playing(true);
    auto heard = render(engine, 2 * bar_samples - block);
    // The right is the return alone: the send, post-fader and pre-pan.
    require(near(rms(heard.right, 10000, 86000), level * 0.1, 1e-5) &&
                near(rms(heard.right, 106000, 186000), level, 1e-5),
            "the post-fader send follows the lane: " + text(rms(heard.right, 10000, 86000)) +
                ' ' + text(rms(heard.right, 106000, 186000)));
    require(near(rms(heard.left, 10000, 86000), level * 0.1, 1e-5),
            "and so does the direct path");

    // Pre-fader, the send ignores the fader lane.
    song.tracks[0].sends[0].pre_fader = true;
    engine.apply_mix(song);
    engine.seek(0);
    heard = render(engine, 2 * bar_samples - block);
    require(near(rms(heard.right, 10000, 86000), level, 1e-5),
            "a pre-fader send takes the signal before the fader lane");

    // A mute lane silences pre-fader sends too.
    song.tracks[0].automation.push_back(
        {strip(AutomationTarget::Kind::mute), {{0, 0.0}, {bar, 0.0}, {bar, 1.0}}});
    recompile(engine, song);
    engine.seek(0);
    heard = render(engine, 2 * bar_samples - block);
    require(near(rms(heard.right, 10000, 86000), level, 1e-5) &&
                rms(heard.right, 106000, 186000) < 1e-7 && rms(heard.left, 106000, 186000) < 1e-7,
            "muted by its lane, the track sends nothing");
}

// Scenario: Passes are recorded by mode, split at the loop, and thinned.
void recorder_passes() {
    auto song = held_song(2, 1000);
    song.returns = {ReturnBus{}};
    song.returns[0].inserts = {clap_effect()};
    const auto gain = strip(AutomationTarget::Kind::gain);
    const auto move = [](double value, bool touching, Tick at) {
        return StripMoveEvent{{0, StripControl::gain, value, touching}, 0, at};
    };

    // Touch: a pass from the hold to the release; the lane resumes after it.
    song.tracks[0].automation_mode = AutomationMode::touch;
    AutomationTake take(song.length());
    take.record(song, move(0.0, true, 100));
    take.record(song, move(-6.0, true, 200));
    take.record(song, move(-6.0, false, 300));
    require(take.commit(song) == 1 && song.tracks[0].automation.size() == 1, "a gain lane is made");
    const auto& lane = song.tracks[0].automation[0];
    require(*lane.value_at(50) == 0.0 && *lane.value_at(150) == 0.0 &&
                *lane.value_at(250) == -6.0 && *lane.value_at(299) == -6.0 &&
                *lane.value_at(300) == 0.0 && *lane.value_at(900) == 0.0,
            "the lane steps to -6 dB between the moves and resumes 0 dB after the release");

    // A hold that never moved the control makes no lane.
    auto untouched = held_song(1, 1000);
    untouched.tracks[0].automation_mode = AutomationMode::touch;
    AutomationTake still(1000);
    still.record(untouched, move(0.0, true, 100));
    still.record(untouched, move(0.0, false, 200));
    require(still.commit(untouched) == 0 && untouched.tracks[0].automation.empty(),
            "a fader held and let go unmoved writes nothing");

    // Read and off record nothing.
    untouched.tracks[0].automation_mode = AutomationMode::read;
    still.record(untouched, move(0.0, true, 100));
    still.record(untouched, move(-3.0, false, 200));
    still.finish(300);
    require(!still.ready() && untouched.tracks[0].automation.empty(), "read records nothing");

    // Latch: the last value holds until the stop; a pass over the loop point
    // is split there.
    auto looped = held_song(1, 1000);
    looped.tracks[0].automation_mode = AutomationMode::latch;
    AutomationTake latch(looped.length());
    latch.record(looped, move(0.0, true, 900));
    latch.record(looped, move(-3.0, false, 900));
    latch.record(looped, move(-9.0, true, 100)); // after the wrap
    latch.record(looped, move(-9.0, false, 100));
    latch.finish(200);
    require(latch.commit(looped) == 2, "the pass is written in two parts, either side of the loop");
    const auto& wrapped = looped.tracks[0].automation.at(0);
    require(*wrapped.value_at(850) == 0.0 && *wrapped.value_at(950) == -3.0 &&
                *wrapped.value_at(50) == -3.0 && *wrapped.value_at(150) == -9.0 &&
                *wrapped.value_at(250) == 0.0,
            "latch holds to the loop point, on from the top, and until the stop");

    // A straight fader sweep is thinned to its two ends.
    auto swept = held_song(1, 2000);
    swept.tracks[0].automation_mode = AutomationMode::write;
    AutomationTake sweep(2000);
    sweep.touch(0, gain, 0, 0.0);
    for (Tick at = 0; at <= 1000; at += 10)
        sweep.value(0, gain, at, -0.02 * static_cast<double>(at));
    sweep.finish(1000);
    require(sweep.commit(swept) == 1, "the sweep is written");
    require(swept.tracks[0].automation[0].points.size() < 12,
            "a sweep of a hundred steps is thinned: " +
                std::to_string(swept.tracks[0].automation[0].points.size()) + " points");

    // A return's insert is recorded into the track that holds its lane, and
    // not at all when none does.
    const ProcessorAddress returned{BusKind::ret, 0, 0};
    const auto edit = [&](ParameterEdit::Kind kind, double value) {
        return PluginEditEvent{returned, 0, true, {kind, 0, value, 0}};
    };
    auto routed = held_song(2, 1000);
    routed.returns = {ReturnBus{}};
    routed.returns[0].inserts = {clap_effect()};
    routed.tracks[1].automation_mode = AutomationMode::touch;
    AutomationTake plugin(1000);
    plugin.record(routed, edit(ParameterEdit::Kind::begin, 0.0), 100, 0.25);
    plugin.record(routed, edit(ParameterEdit::Kind::value, 0.5), 100, 0.25);
    plugin.record(routed, edit(ParameterEdit::Kind::end, 0.0), 200, 0.25);
    require(!plugin.ready(), "no track holds a lane for the return's insert: nothing recorded");
    routed.tracks[1].automation.push_back({parameter(returned, 0), {{0, 0.3}}});
    plugin.record(routed, edit(ParameterEdit::Kind::begin, 0.0), 100, 0.25);
    plugin.record(routed, edit(ParameterEdit::Kind::value, 0.6), 150, 0.25);
    plugin.record(routed, edit(ParameterEdit::Kind::end, 0.0), 200, 0.25);
    require(plugin.commit(routed) == 1, "the edit is recorded into track 1's lane");
    const auto& returned_lane = routed.tracks[1].automation[0];
    require(*returned_lane.value_at(160) == 0.6 && *returned_lane.value_at(250) == 0.3,
            "the return's lane holds the turn and resumes after it");
}

} // namespace

int main(int argc, char** argv) {
    const std::map<std::string, void (*)()> cases{
        {"gain_ramp_bounce", gain_ramp_bounce},
        {"pan_and_mute_lanes", pan_and_mute_lanes},
        {"touch_override", touch_override},
        {"strip_moves_replay", strip_moves_replay},
        {"plugin_moves_lane", plugin_moves_lane},
        {"chase_on_seek", chase_on_seek},
        {"effect_lane_on_return", effect_lane_on_return},
        {"round_trip", round_trip},
        {"solo_with_lane", solo_with_lane},
        {"post_fader_send", post_fader_send},
        {"recorder_passes", recorder_passes},
    };
    if (argc != 2 || !cases.contains(argv[1])) {
        std::cerr << "usage: blokkily_automation_tests <case>\n";
        return 2;
    }
    try {
        cases.at(argv[1])();
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << argv[1] << ": " << error.what() << '\n';
        return 1;
    }
    std::cout << "PASS " << argv[1] << '\n';
    return 0;
}
