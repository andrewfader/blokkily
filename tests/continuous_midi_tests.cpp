// Continuous MIDI (wave 4.1): the pitch wheel, control changes, pressure and
// the sustain pedal, proved from rendered audio through real instruments -
// FluidSynth with a real SF2, the VST3 fixture bundle through the production
// JUCE adapter, the CLAP fixture through the production CLAP adapter and the
// built-in sampler - and controller movements recorded into the canonical
// pattern, saved, chased and exported.
//
// features/continuous_midi.feature: each case names its scenario.

#include "support/audio_probe.hpp"

#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"
#include "blokkily/instruments/sampler.hpp"
#include "blokkily/instruments/soundfont_synth.hpp"
#include "blokkily/midi/midi_input.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/vst3_instance.hpp"
#include "blokkily/project/project.hpp"
#include "blokkily/sequencer/take.hpp"

#include <clap/events.h>
#include <dlfcn.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;
namespace fs = std::filesystem;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 256;
// Two semitones either way, the wheel's range everywhere here.
const double a4 = 440.0;
const double a4_bent_up = 440.0 * std::pow(2.0, 2.0 / 12.0);         // 493.88

std::uint32_t bend_raw(int value, int channel = 0) {
    return static_cast<std::uint32_t>(0xE0 | channel) |
           (static_cast<std::uint32_t>(value & 0x7F) << 8) |
           (static_cast<std::uint32_t>((value >> 7) & 0x7F) << 16);
}

PluginEvent raw_event(std::uint32_t raw, std::uint32_t offset = 0) {
    return {PluginEvent::Type::midi_raw, offset, static_cast<std::int32_t>(raw), 0.0};
}

std::unique_ptr<PluginInstance> soundfont() {
    auto synth = std::make_unique<SoundFontSynth>();
    require(synth->load(BLOKKILY_TEST_SF2_PATH), "the SF2 fixture must load into FluidSynth");
    return synth;
}

std::unique_ptr<PluginInstance> vst3() {
    std::string error;
    auto plugin = Vst3PluginInstance::create(BLOKKILY_TEST_VST3_PATH, 0, &error);
    require(plugin != nullptr, "the VST3 fixture must load through JUCE: " + error);
    return plugin;
}

std::unique_ptr<PluginInstance> clap() {
    std::string error;
    auto plugin = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH, "dev.blokkily.test", &error);
    require(plugin != nullptr, "the CLAP fixture must load: " + error);
    return plugin;
}

// One track, one instrument, the production callback, a deterministic MIDI
// keyboard routed to the track.
struct Rig {
    Song song;
    SongEngine engine;
    RtAudioOutput output{RtAudioOutput::Mode::deterministic};
    MidiInput keyboard{MidiInput::Mode::deterministic};

    explicit Rig(std::unique_ptr<PluginInstance> instrument, Song arranged = {}) {
        song = std::move(arranged);
        if (song.patterns.empty()) song.patterns = {{"P", Pattern(1920, 480)}};
        if (song.tracks.empty()) song.tracks = {Track{}};
        engine.set_instrument(0, std::move(instrument));
        std::string error;
        require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
        require(keyboard.open(std::size_t{0}, &error), "open the deterministic input: " + error);
        keyboard.set_track(0);
        engine.connect_input(&keyboard.queue());
        require(output.open(engine) && output.start(), "the production output must start");
    }
    void send(std::uint8_t status, int first, int second) {
        const std::array<std::uint8_t, 3> message{status, static_cast<std::uint8_t>(first),
                                                  static_cast<std::uint8_t>(second)};
        require(keyboard.inject(message), "the keyboard takes the message");
    }
    std::vector<float> render(std::size_t frames) {
        std::vector<float> left;
        std::vector<float> stereo;
        while (left.size() < frames) {
            const auto now = std::min<std::size_t>(block, frames - left.size());
            stereo.assign(now * 2, 0.0F);
            require(output.pump(stereo), "the production callback must render");
            left.insert(left.end(), stereo.begin(), stereo.begin() + static_cast<long>(now));
        }
        return left;
    }
    void play(const PluginEvent& event) {
        require(engine.play_live(0, event), "the live queue takes the event");
    }
};

double frequency(const std::vector<float>& heard, std::size_t from, std::size_t count,
                 double low, double high) {
    return probe::dominant_frequency({heard.data() + from, count}, rate, low, high, 0.5);
}

void require_frequency(double measured, double expected, const std::string& what) {
    require(std::abs(measured - expected) <= 2.0,
            what + ": " + std::to_string(measured) + " Hz, expected " + std::to_string(expected));
}

// features/continuous_midi.feature:
//   Scenario: A keyboard on any channel bends the SoundFont's notes
void soundfont_bend() {
    Rig rig(soundfont());
    // Channel 2: the note and the wheel both arrive there.
    rig.send(0x91, 69, 110);
    const auto straight = rig.render(26000);
    require(probe::rms(straight) > 1e-3, "the SoundFont note sounds");
    // The bank's sample is tuned as its author tuned it; what the wheel does
    // is measured against the pitch the note has.
    const double own = frequency(straight, 2000, 24000, 400, 540);
    require(std::abs(own - a4) < 0.02 * a4, "the note is an A4: " + std::to_string(own));
    const auto ratio_is = [](double measured, double base, double semitones, const std::string& what) {
        const double want = base * std::pow(2.0, semitones / 12.0);
        // The bank's voice has a slow vibrato; over half a second it averages
        // out to within a percent, well inside the quarter tone (3 %) the
        // retuned cases tell apart.
        require(std::abs(measured - want) <= 0.012 * want,
                what + ": " + std::to_string(measured) + " Hz, expected " + std::to_string(want));
    };
    rig.send(0xE1, 0x7F, 0x7F);   // the wheel all the way up
    const auto bent = rig.render(26000);
    ratio_is(frequency(bent, 2000, 24000, 400, 560), own, 2.0,
             "a wheel on channel 2 bends the note two semitones");
    rig.send(0xE1, 0x00, 0x40);   // centred
    rig.send(0x81, 69, 0);
    (void)rig.render(24000);

    // A microtonal note plays on a retuned channel of its own; the wheel
    // reaches it too.
    rig.play({PluginEvent::Type::note_on, 0, 69, 0.9, 50.0});
    const auto quarter = rig.render(26000);
    ratio_is(frequency(quarter, 2000, 24000, 400, 560), own, 0.5, "the retuned note before the bend");
    rig.play(raw_event(bend_raw(16383, 5)));
    const auto quarter_bent = rig.render(26000);
    ratio_is(frequency(quarter_bent, 2000, 24000, 400, 560), own, 2.5,
             "the wheel bends a retuned note as well");
}

// features/continuous_midi.feature:
//   Scenario: The sustain pedal holds SoundFont notes until it is released
void soundfont_sustain() {
    const auto held_level = [](bool pedal) {
        Rig rig(soundfont());
        rig.send(0x90, 60, 120);
        (void)rig.render(4800);
        if (pedal) rig.send(0xB0, 64, 127);
        rig.send(0x80, 60, 0);
        (void)rig.render(24000);   // half a second after the key came up
        const auto after = rig.render(4800);
        double released = 0.0;
        if (pedal) {
            rig.send(0xB0, 64, 0);
            (void)rig.render(48000);
            released = probe::rms(rig.render(4800));
        }
        return std::pair{probe::rms(after), released};
    };
    const auto [with_pedal, after_release] = held_level(true);
    const auto [without_pedal, unused] = held_level(false);
    (void)unused;
    require(with_pedal > 1e-3, "with the pedal down the note still sounds after its key is up");
    require(with_pedal > 4.0 * without_pedal,
            "the pedal holds the note: " + std::to_string(with_pedal) + " against " +
                std::to_string(without_pedal) + " without it");
    require(after_release < 0.25 * with_pedal,
            "releasing the pedal lets the note decay: " + std::to_string(after_release));
}

// features/continuous_midi.feature:
//   Scenario: The pitch wheel reaches a VST3 instrument through the JUCE adapter
void vst3_bend() {
    Rig rig(vst3());
    rig.play({PluginEvent::Type::parameter_value, 0, 1, 1.0});   // the fixture's tone
    rig.send(0x92, 69, 100);   // channel 3
    const auto straight = rig.render(9600);
    require_frequency(frequency(straight, 1200, 8400, 400, 540), a4, "the VST3 note");
    rig.send(0xE2, 0x7F, 0x7F);
    const auto bent = rig.render(9600);
    require_frequency(frequency(bent, 1200, 8400, 400, 540), a4_bent_up,
                      "a wheel on channel 3 bends the VST3 note two semitones");
    rig.send(0x82, 69, 0);
    rig.send(0xE2, 0x00, 0x40);
    (void)rig.render(2400);
    // A retuned note sits on a channel bent for its cents; the wheel is
    // added to that bend rather than replacing it.
    // (Half the wheel's travel: a semitone, plus the note's half.)
    rig.play({PluginEvent::Type::note_on, 0, 69, 0.8, 50.0});
    rig.play(raw_event(bend_raw(8192 + 4096)));
    const auto both = rig.render(9600);
    require_frequency(frequency(both, 1200, 8400, 400, 540), 440.0 * std::pow(2.0, 1.5 / 12.0),
                      "the wheel adds to a retuned VST3 note's own bend");
}

fs::path work_dir(const std::string& name) {
    const fs::path dir = fs::path(BLOKKILY_CONTINUOUS_WORK) / name;
    std::error_code ignored;
    fs::remove_all(dir, ignored);
    fs::create_directories(dir);
    return dir;
}

// features/continuous_midi.feature:
//   Scenario: The sampler follows the pitch wheel and the sustain pedal
void sampler_bend_sustain() {
    const auto dir = work_dir("sampler");
    const auto file = dir / "a440.wav";
    {
        std::vector<float> tone(96000);
        for (std::size_t i = 0; i < tone.size(); ++i)
            tone[i] = 0.5F * static_cast<float>(std::sin(2.0 * std::numbers::pi * 440.0 *
                                                         static_cast<double>(i) / rate));
        WaveWriter writer;
        std::string error;
        require(writer.open(file, 48000, WaveFormat::float32, &error) &&
                    writer.write(tone, tone, &error) && writer.close(&error),
                "write the sample: " + error);
    }
    SamplerZone zone;
    zone.sample = file.string();
    zone.root_key = 69;
    zone.loop = LoopMode::forward;
    zone.loop_start = 0;
    zone.loop_end = 96000;   // 880 whole cycles: a seamless loop
    zone.envelope.attack_s = 0.0;
    zone.envelope.release_s = 0.02;
    SamplerProgram program;
    program.zones = {zone};
    auto sampler = std::make_unique<SamplerInstrument>();
    std::string error;
    require(sampler->set_program(program, &error), "the sampler takes the program: " + error);

    Rig rig(std::move(sampler));
    rig.send(0x90, 69, 110);
    const auto straight = rig.render(9600);
    require_frequency(frequency(straight, 1200, 8400, 400, 540), a4, "the sampled note");
    rig.send(0xE0, 0x7F, 0x7F);
    const auto bent = rig.render(9600);
    require_frequency(frequency(bent, 1200, 8400, 400, 540), a4_bent_up,
                      "the wheel bends the sampler two semitones");
    rig.send(0xE0, 0x00, 0x40);
    // The sampler's own pedal, for a CC 64 that reaches it as MIDI (from a
    // pattern or a plugin host), not through the keyboard's note holding.
    rig.play(raw_event(0xB0U | (64U << 8) | (127U << 16)));
    rig.play({PluginEvent::Type::note_off, 0, 69, 0.0});
    (void)rig.render(4800);
    const auto held = rig.render(4800);
    require(probe::rms(held) > 0.1, "the pedal holds the sampled note past its note-off");
    rig.play(raw_event(0xB0U | (64U << 8)));
    (void)rig.render(4800);
    require(probe::peak(rig.render(4800)) < 1e-6, "pedal up: the note is released");
}

// A pattern with one long A4 on step 0 and the given controller movements.
Song bend_song(const std::vector<ContinuousEvent>& controls, Tick note_start = 0,
               Tick note_length = 1900) {
    Song song;
    Pattern pattern(1920, 480);
    Trigger note;
    note.start = note_start;
    note.duration = note_length;
    note.musical_data = Note{69, 0.9F, 0.0F, 0.0};
    (void)pattern.add(note);
    for (const auto& control : controls) pattern.add_continuous(control);
    song.patterns = {{"P", std::move(pattern)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};
    return song;
}

// The CLAP fixture as a sine voice, rendered directly with `events`.
std::vector<float> clap_reference(std::vector<PluginEvent> events, std::size_t frames) {
    auto plugin = clap();
    require(plugin->activate(rate, 1, static_cast<std::uint32_t>(frames)), "activate the reference");
    std::vector<float> left(frames), right(frames);
    plugin->process({left, right}, events);
    return left;
}

// features/continuous_midi.feature:
//   Scenario: A recorded bend plays back on its exact sample
void pattern_sample_accurate() {
    // Tick 250 at 120 BPM and 480 ticks a beat is sample 12500.
    const auto song = bend_song({{250, ContinuousEvent::Kind::pitch_bend, 0, 16383}});
    Rig rig(clap(), song);
    rig.play({PluginEvent::Type::parameter_value, 0, 1, 1.0});   // tone
    rig.engine.set_playing(true);
    const auto heard = rig.render(20000);
    const float centre = strip_gain(MixerStrip{}, false).left;
    const auto against = [&](std::uint32_t at) {
        const auto reference = clap_reference(
            {{PluginEvent::Type::parameter_value, 0, 1, 1.0},
             {PluginEvent::Type::note_on, 0, 69, 0.9},
             raw_event(bend_raw(16383), at)},
            20000);
        double worst = 0.0;
        for (std::size_t i = 0; i < reference.size(); ++i)
            worst = std::max(worst, static_cast<double>(std::abs(heard[i] - reference[i] * centre)));
        return worst;
    };
    const double exact = against(12500);
    require(exact < 1e-5, "the bend lands on sample 12500: " + std::to_string(exact));
    require(against(12499) > 1e-4 && against(12501) > 1e-4,
            "and not a sample either side of it");
}

// features/continuous_midi.feature:
//   Scenario: Controllers are chased when the playhead jumps
void chase() {
    // The wheel goes up at tick 100 and nothing brings it back within the
    // pattern. One note starts before it (tick 10) and one after (tick 200).
    Song song;
    Pattern pattern(1920, 480);
    for (const auto& [start, length] : {std::pair<Tick, Tick>{10, 60}, {200, 400}}) {
        Trigger note;
        note.start = start;
        note.duration = length;
        note.musical_data = Note{69, 0.9F, 0.0F, 0.0};
        (void)pattern.add(note);
    }
    pattern.add_continuous({100, ContinuousEvent::Kind::pitch_bend, 0, 16383});
    song.patterns = {{"P", std::move(pattern)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};
    Rig rig(vst3(), song);
    rig.play({PluginEvent::Type::parameter_value, 0, 1, 1.0});
    rig.engine.set_playing(true);
    const std::size_t per_tick = 50;
    const auto length = static_cast<std::size_t>(rig.engine.song_samples());
    const auto first = rig.render(length);
    const auto second = rig.render(length);   // across the wrap
    require_frequency(frequency(first, 12 * per_tick, 2400, 400, 540), a4,
                      "the first note, before the wheel moves");
    require_frequency(frequency(first, 220 * per_tick, 9600, 400, 540), a4_bent_up,
                      "the second note, after it");
    require_frequency(frequency(second, 12 * per_tick, 2400, 400, 540), a4,
                      "after the wrap the wheel is back at rest for the first note");
    // A locate past the movement plays the wheel where it had got to.
    rig.engine.set_playing(false);
    (void)rig.render(block);
    rig.engine.seek(150 * per_tick);
    rig.engine.set_playing(true);
    const auto located = rig.render(12000);
    require_frequency(frequency(located, 60 * per_tick, 9600, 400, 540), a4_bent_up,
                      "a locate to after the bend chases it");
}

// features/continuous_midi.feature:
//   Scenario: Controllers played while recording are written into the pattern
void record_take() {
    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};
    Rig rig(clap(), song);
    rig.engine.set_recording(true);
    rig.engine.set_playing(true);
    (void)rig.render(10 * block);
    rig.send(0x93, 60, 100);   // channel 4
    rig.send(0xE3, 0x00, 0x60);
    rig.send(0xB3, 1, 90);     // mod wheel
    rig.send(0xB3, 64, 127);   // the pedal is the keyboard's, not a pattern's
    (void)rig.render(10 * block);
    rig.send(0xD3, 77, 0);
    (void)rig.render(block);
    CapturedEvent captured;
    TakeRecorder recorder(song.length());
    std::vector<ContinuousEvent> heard;
    std::vector<Tick> at;
    while (rig.engine.take_captured(captured)) {
        if (captured.event.type != PluginEvent::Type::midi_raw) continue;
        if (auto control = recorder.control(
                captured.tick, static_cast<std::uint32_t>(captured.event.key_or_parameter))) {
            heard.push_back(*control);
            at.push_back(captured.tick);
        }
    }
    require(heard.size() == 3, "the wheel, the mod wheel and pressure are captured, the pedal "
                               "is not: " + std::to_string(heard.size()));
    require(heard[0].kind == ContinuousEvent::Kind::pitch_bend && heard[0].value == (0x60 << 7),
            "the wheel's value");
    require(heard[1].kind == ContinuousEvent::Kind::control_change && heard[1].controller == 1 &&
                heard[1].value == 90,
            "the mod wheel's value");
    require(heard[2].kind == ContinuousEvent::Kind::channel_pressure && heard[2].value == 77,
            "the pressure's value");
    for (auto control : heard) {
        const auto target = take_target(song, 0, control.tick, 0);
        control.tick = target.offset;
        write_continuous(song.patterns[target.pattern].pattern, control);
    }
    const auto& written = song.patterns[0].pattern.continuous();
    require(written.size() == 3 && written[0].tick == at[0] && written[2].tick == at[2],
            "the pattern holds the movements where they were heard");
    require(at[2] > at[0], "a later movement lands later");

    // Saved and loaded, byte for byte; an older file without them loads.
    Project project;
    project.song = song;
    const auto text = ProjectFile::serialize(project);
    require(text.find("control 0 ") != std::string::npos, "the movements are saved");
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value(), "the project loads: " + error);
    const auto& back = loaded->song.patterns[0].pattern.continuous();
    require(std::vector(back.begin(), back.end()) == std::vector(written.begin(), written.end()),
            "and holds the same movements");
    require(ProjectFile::serialize(*loaded) == text, "a byte-identical round trip");
    std::string old_text;
    for (std::size_t from = 0; from < text.size();) {
        const auto end = text.find('\n', from);
        const auto line = text.substr(from, end - from + 1);
        if (line.rfind("control ", 0) != 0) old_text += line;
        from = end + 1;
    }
    const auto old = ProjectFile::parse(old_text, &error);
    require(old.has_value() && old->song.patterns[0].pattern.continuous().empty(),
            "a file from before wave 4.1 loads with no movements: " + error);
    for (const char* bad : {"control 0 99999 bend 8192\n", "control 0 10 bend 20000\n",
                            "control 0 10 cc 64 127\n", "control 7 10 pressure 3\n",
                            "control 0 10 wiggle 3\n"}) {
        require(!ProjectFile::parse(text + bad, &error).has_value(),
                std::string("a malformed control record is refused: ") + bad);
    }
    // The pattern refuses a movement outside itself or its range.
    require(song.consistent(), "the recorded song is consistent");
    for (const ContinuousEvent& bad : {ContinuousEvent{1920, ContinuousEvent::Kind::pitch_bend, 0, 1},
                                       ContinuousEvent{0, ContinuousEvent::Kind::pitch_bend, 0, 16384},
                                       ContinuousEvent{0, ContinuousEvent::Kind::control_change, 64, 1}}) {
        bool refused = false;
        try {
            song.patterns[0].pattern.add_continuous(bad);
        } catch (const std::invalid_argument&) {
            refused = true;
        }
        require(refused, "an invalid movement is refused by the pattern");
    }
}

// features/continuous_midi.feature:
//   Scenario: An export plays the controller movements the engine plays
void export_matches() {
    const auto song = bend_song({{300, ContinuousEvent::Kind::pitch_bend, 0, 16383},
                                 {900, ContinuousEvent::Kind::pitch_bend, 0, 4000},
                                 {1200, ContinuousEvent::Kind::control_change, 1, 64}});
    Rig live(vst3(), song);
    live.play({PluginEvent::Type::parameter_value, 0, 1, 1.0});
    (void)live.render(block);
    live.engine.set_playing(true);
    const auto frames = static_cast<std::size_t>(live.engine.song_samples());
    std::vector<float> heard_left, heard_right;
    while (heard_left.size() < frames) {
        const auto now = std::min<std::size_t>(block, frames - heard_left.size());
        std::vector<float> stereo(now * 2);
        require(live.output.pump(stereo), "render");
        heard_left.insert(heard_left.end(), stereo.begin(), stereo.begin() + static_cast<long>(now));
        heard_right.insert(heard_right.end(), stereo.begin() + static_cast<long>(now), stereo.end());
    }
    require_frequency(frequency(heard_left, 20000, 9600, 400, 540), a4_bent_up,
                      "the bend is heard");

    Rig exporter(vst3(), song);
    exporter.play({PluginEvent::Type::parameter_value, 0, 1, 1.0});
    (void)exporter.render(block);
    const auto dir = work_dir("export");
    const auto file = dir / "bounce.wav";
    std::string error;
    const auto report = bounce_song(exporter.engine, file, WaveFormat::float32, 0, &error);
    require(report.has_value(), "bounce: " + error);
    const auto read = read_wave(file, &error);
    require(read.has_value() && read->frames == frames, "the export reads back: " + error);
    for (std::size_t i = 0; i < frames; ++i)
        require(read->interleaved[i * 2] == heard_left[i] &&
                    read->interleaved[i * 2 + 1] == heard_right[i],
                "export frame " + std::to_string(i) + " is what the engine played");
}

// features/continuous_midi.feature:
//   Scenario: Poly pressure is recorded, played at its sample, chased and exported
void poly_pressure() {
    // --- Recorded from the keyboard, through a key map that sends key 60 to
    // 62: the pressure names the key the instrument was told. ---------------
    Song empty;
    empty.patterns = {{"P", Pattern(1920, 480)}};
    empty.tracks = {Track{}};
    empty.clips = {{0, 0, 0, 1}};
    Rig rig(clap(), empty);
    auto map = identity_key_map();
    map[60] = {62, 0.0F};
    rig.keyboard.set_key_map(map);
    rig.engine.set_recording(true);
    rig.engine.set_playing(true);
    (void)rig.render(10 * block);
    rig.send(0x90, 60, 127);
    const auto untouched = rig.render(4 * block);
    rig.send(0xA0, 60, 127);
    const auto pressed = rig.render(4 * block);
    rig.send(0xA0, 60, 30);
    (void)rig.render(4 * block);
    rig.send(0x80, 60, 0);
    (void)rig.render(2 * block);
    const float plain = untouched.back();
    require(plain > 0.01F && std::abs(pressed.back() - 2.0F * plain) < 1e-4F,
            "poly pressure played live doubles the CLAP fixture's pressed key: " +
                std::to_string(plain) + " -> " + std::to_string(pressed.back()));
    CapturedEvent captured;
    TakeRecorder recorder(empty.length());
    std::vector<ContinuousEvent> heard;
    while (rig.engine.take_captured(captured)) {
        if (captured.event.type != PluginEvent::Type::midi_raw) continue;
        if (auto control = recorder.control(
                captured.tick, static_cast<std::uint32_t>(captured.event.key_or_parameter)))
            heard.push_back(*control);
    }
    require(heard.size() == 2 && heard[0].kind == ContinuousEvent::Kind::poly_pressure &&
                heard[0].controller == 62 && heard[0].value == 127 && heard[1].value == 30 &&
                heard[1].tick > heard[0].tick,
            "both presses are captured, on the mapped key: " + std::to_string(heard.size()));
    for (auto control : heard) {
        control.tick = take_target(empty, 0, control.tick, 0).offset;
        write_continuous(empty.patterns[0].pattern, control);
    }
    require(empty.patterns[0].pattern.continuous().size() == 2, "and written into the pattern");

    // --- Played back at its sample and chased. Note A (0..240) and note B
    // (600..1900) on key 62; the key is pressed fully at tick 250, between
    // them, and nothing lets it go. --------------------------------------------
    Song song;
    Pattern pattern(1920, 480);
    for (const auto& [start, length] : {std::pair<Tick, Tick>{0, 240}, {600, 1300}}) {
        Trigger note;
        note.start = start;
        note.duration = length;
        note.musical_data = Note{62, 1.0F, 0.0F, 0.0};
        (void)pattern.add(note);
    }
    pattern.add_continuous({250, ContinuousEvent::Kind::poly_pressure, 62, 127});
    song.patterns = {{"P", std::move(pattern)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};
    std::string why;
    require(song.consistent(&why), "the song is valid: " + why);
    SongEngine engine;
    engine.set_instrument(0, clap());
    require(engine.prepare(song, rate, block, 0, &why), "prepare: " + why);
    const auto render = [&engine](std::size_t frames) {
        std::vector<float> left(frames), right(frames);
        for (std::size_t done = 0; done < frames; done += block) {
            const auto now = std::min<std::size_t>(block, frames - done);
            engine.process({std::span(left).subspan(done, now), std::span(right).subspan(done, now)});
        }
        return left;
    };
    engine.set_playing(true);
    const float level = 0.25F * static_cast<float>(std::cos(std::numbers::pi / 4.0));
    const std::size_t per_tick = 50;
    const auto length = static_cast<std::size_t>(engine.song_samples());
    const auto first = render(length);
    const auto near = [](float actual, float expected) { return std::abs(actual - expected) < 1e-5F; };
    require(near(first[100 * per_tick], level) && near(first[700 * per_tick], 2.0F * level),
            "note A sounds unpressed, note B pressed from the movement between them: " +
                std::to_string(first[100 * per_tick]) + " / " + std::to_string(first[700 * per_tick]));
    // Across the wrap the key's pressure is chased back to rest for note A.
    const auto second = render(100 * per_tick);
    require(near(second[50 * per_tick], level),
            "after the wrap note A is unpressed again (chased to rest): " +
                std::to_string(second[50 * per_tick]));
    // A locate past the movement plays the pressure it had reached: note B
    // is pressed again, although note A's chase had let the key go.
    engine.seek(400 * per_tick);
    const auto located = render(400 * per_tick);
    require(near(located[300 * per_tick], 2.0F * level),
            "a locate past the movement chases it for note B: " +
                std::to_string(located[300 * per_tick]));

    // --- Sample-accurate: the pressure lands on tick 250's sample, 12500.
    {
        Song exact = song;
        Pattern held(1920, 480);
        Trigger note;
        note.start = 0;
        note.duration = 1900;
        note.musical_data = Note{62, 1.0F, 0.0F, 0.0};
        (void)held.add(note);
        held.add_continuous({250, ContinuousEvent::Kind::poly_pressure, 62, 127});
        exact.patterns = {{"P", std::move(held)}};
        SongEngine timed;
        timed.set_instrument(0, clap());
        require(timed.prepare(exact, rate, block, 0, &why), "prepare: " + why);
        timed.set_playing(true);
        std::vector<float> left(80 * block), right(80 * block);
        for (std::size_t done = 0; done < left.size(); done += block)
            timed.process({std::span(left).subspan(done, block), std::span(right).subspan(done, block)});
        require(near(left[12499], level) && near(left[12500], 2.0F * level),
                "the pressure lands on its sample: " + std::to_string(left[12499]) + " / " +
                    std::to_string(left[12500]));
    }

    // --- Exported: the file reads back as the engine plays. ----------------
    const fs::path file = fs::path(BLOKKILY_CONTINUOUS_WORK) / "poly-pressure.wav";
    fs::create_directories(file.parent_path());
    std::string error;
    require(bounce_song(engine, file, WaveFormat::float32, 0, &error).has_value(),
            "the bounce is written: " + error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value() && wave->frames >= length, "the bounce reads back: " + error);
    engine.seek(0);
    engine.set_playing(true);
    const auto live = render(length);
    double worst = 0.0;
    for (std::size_t frame = 0; frame < length; ++frame)
        worst = std::max(worst, static_cast<double>(std::abs(wave->interleaved[2 * frame] - live[frame])));
    require(worst < 1e-6 && near(wave->interleaved[2 * 700 * per_tick], 2.0F * level),
            "the export plays the pressure the engine plays, worst " + std::to_string(worst));

    // --- Saved as a control record; older files and bad records. -----------
    Project project;
    project.song = song;
    const auto text = ProjectFile::serialize(project);
    require(text.find("control 0 250 poly 62 127\n") != std::string::npos,
            "the pressure is saved with its key");
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value() && ProjectFile::serialize(*loaded) == text,
            "and reads back byte for byte: " + error);
    auto older = text;
    older.erase(older.find("control 0 250 poly 62 127\n"), std::string("control 0 250 poly 62 127\n").size());
    const auto old = ProjectFile::parse(older, &error);
    require(old.has_value() && old->song.patterns[0].pattern.continuous().empty(),
            "a file from before poly pressure loads with none: " + error);
    for (const char* bad : {"control 0 10 poly 128 1\n", "control 0 10 poly 60 128\n",
                            "control 0 10 poly 60\n", "control 0 1920 poly 60 1\n"})
        require(!ProjectFile::parse(text + bad, &error).has_value(),
                std::string("a malformed poly record is refused: ") + bad);
}

// --- MPE (per-note expression) ------------------------------------------------

struct ExpressionSeen {
    int id;
    int key;
    double value;
    std::uint32_t time;
};

// The note expressions the CLAP fixture was sent since the last call, from
// its exported log.
std::vector<ExpressionSeen> clap_expressions() {
    void* library = dlopen(BLOKKILY_TEST_CLAP_PATH, RTLD_NOW | RTLD_NOLOAD);
    require(library != nullptr, "the CLAP fixture is loaded");
    using Report = int (*)(int*, int*, double*, std::uint32_t*, int);
    using Clear = void (*)();
    auto* report = reinterpret_cast<Report>(dlsym(library, "blokkily_test_expressions"));
    auto* clear = reinterpret_cast<Clear>(dlsym(library, "blokkily_test_clear_expressions"));
    require(report != nullptr && clear != nullptr, "the fixture reports its note expressions");
    std::array<int, 4096> ids{}, keys{};
    std::array<double, 4096> values{};
    std::array<std::uint32_t, 4096> times{};
    const int seen = std::min(report(ids.data(), keys.data(), values.data(), times.data(), 4096), 4096);
    std::vector<ExpressionSeen> out;
    for (int index = 0; index < seen; ++index)
        out.push_back({ids[static_cast<std::size_t>(index)], keys[static_cast<std::size_t>(index)],
                       values[static_cast<std::size_t>(index)],
                       times[static_cast<std::size_t>(index)]});
    dlclose(library);
    clear();
    return out;
}

constexpr int clap_tuning = CLAP_NOTE_EXPRESSION_TUNING;
constexpr int clap_brightness = CLAP_NOTE_EXPRESSION_BRIGHTNESS;
constexpr int clap_pressure = CLAP_NOTE_EXPRESSION_PRESSURE;

std::array<std::uint8_t, 3> bend_bytes(std::uint8_t status, double semitones, double range = 48.0) {
    const int value = std::clamp(static_cast<int>(std::lround(8192.0 + semitones / range * 8192.0)), 0, 16383);
    return {status, static_cast<std::uint8_t>(value & 0x7F), static_cast<std::uint8_t>(value >> 7)};
}

std::vector<float> render_engine(SongEngine& engine, std::size_t frames) {
    std::vector<float> left(frames), right(frames);
    for (std::size_t done = 0; done < frames; done += block) {
        const auto now = std::min<std::size_t>(block, frames - done);
        engine.process({std::span(left).subspan(done, now), std::span(right).subspan(done, now)});
    }
    return left;
}

const float clap_level = 0.25F * static_cast<float>(std::cos(std::numbers::pi / 4.0));
bool close_to(float actual, float expected, float tolerance = 1e-5F) {
    return std::abs(actual - expected) < tolerance;
}

// features/continuous_midi.feature:
//   Scenario: An MPE keyboard plays each note's own expression into CLAP and records it
void mpe_clap_live() {
    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};
    Rig rig(clap(), song);
    // The keyboard turns MPE on itself: RPN 6 on channel 1, fifteen members.
    rig.send(0xB0, 101, 0);
    rig.send(0xB0, 100, 6);
    rig.send(0xB0, 6, 15);
    require(rig.keyboard.mpe().enabled && rig.keyboard.mpe().first == 1 &&
                rig.keyboard.mpe().last == 15,
            "the MPE Configuration Message turns the lower zone on");
    rig.engine.set_recording(true);
    rig.engine.set_playing(true);
    (void)rig.render(10 * block);
    (void)clap_expressions();
    // Member channel 2: the note's starting bend (+12 semitones) and
    // pressure come before its note-on, as MPE sends them.
    const auto up = bend_bytes(0xE1, 12.0);
    rig.send(up[0], up[1], up[2]);
    rig.send(0xD1, 127, 0);
    rig.send(0x91, 69, 100);
    const auto pressed = rig.render(4 * block);
    require(close_to(pressed.back(), 2.0F * clap_level),
            "the note sounds with its own pressure: " + std::to_string(pressed.back()));
    rig.send(0xB1, 74, 127);
    const auto bright = rig.render(4 * block);
    require(close_to(bright.back(), 4.0F * clap_level),
            "its own timbre (CC 74) brightens it: " + std::to_string(bright.back()));
    // A second note on member channel 3 is untouched by the first's
    // expression: the fixture sounds the newest key, unpressed.
    rig.send(0x92, 72, 100);
    const auto second = rig.render(4 * block);
    require(close_to(second.back(), clap_level),
            "another channel's note is not pressed: " + std::to_string(second.back()));
    const auto down = bend_bytes(0xE1, -5.0);
    rig.send(down[0], down[1], down[2]);
    (void)rig.render(2 * block);
    rig.send(0x81, 69, 0);
    rig.send(0x82, 72, 0);
    (void)rig.render(2 * block);
    const auto seen = clap_expressions();
    const auto has = [&](int id, int key, double value) {
        return std::any_of(seen.begin(), seen.end(), [&](const ExpressionSeen& entry) {
            return entry.id == id && entry.key == key && std::abs(entry.value - value) < 0.01;
        });
    };
    require(has(clap_tuning, 69, 12.0) && has(clap_pressure, 69, 1.0) &&
                has(clap_brightness, 69, 1.0) && has(clap_tuning, 69, -5.0) &&
                std::none_of(seen.begin(), seen.end(),
                             [](const ExpressionSeen& entry) { return entry.key == 72; }),
            "CLAP is sent note expressions for key 69 alone: " + std::to_string(seen.size()));

    // The take: each note with its own expression.
    CapturedEvent captured;
    TakeRecorder recorder(song.length());
    std::vector<PlayedNote> played;
    while (rig.engine.take_captured(captured)) {
        const auto key = static_cast<std::int16_t>(captured.event.key_or_parameter);
        if (captured.event.type == PluginEvent::Type::note_on)
            recorder.note_on(captured.tick, key, static_cast<float>(captured.event.value),
                             captured.event.cents);
        else if (captured.event.type == PluginEvent::Type::note_off) {
            if (auto note = recorder.note_off(captured.tick, key)) played.push_back(*note);
        } else if (captured.event.type == PluginEvent::Type::note_expression)
            recorder.expression(captured.tick, key,
                                static_cast<NoteExpression::Kind>(captured.event.expression),
                                static_cast<float>(captured.event.value));
    }
    require(played.size() == 2, "both notes are taken: " + std::to_string(played.size()));
    const auto& first = played[0].key == 69 ? played[0] : played[1];
    const auto& other = played[0].key == 69 ? played[1] : played[0];
    const auto point = [&](NoteExpression::Kind kind, float value) {
        return std::any_of(first.expression.begin(), first.expression.end(),
                           [&](const NoteExpression& at) {
                               return at.kind == kind && std::abs(at.value - value) < 0.01F;
                           });
    };
    require(point(NoteExpression::Kind::pitch, 12.0F) && point(NoteExpression::Kind::pressure, 1.0F) &&
                point(NoteExpression::Kind::timbre, 1.0F) && point(NoteExpression::Kind::pitch, -5.0F) &&
                other.expression.empty(),
            "the first note keeps its bend, pressure and timbre; the second has none");
    const auto late = std::find_if(first.expression.begin(), first.expression.end(),
                                   [](const NoteExpression& at) {
                                       return at.kind == NoteExpression::Kind::pitch &&
                                              std::abs(at.value + 5.0F) < 0.01F;
                                   });
    require(late != first.expression.end() && late->offset > 0,
            "a later movement is kept at its offset into the note");
    Pattern written(1920, 480);
    for (auto note : played) (void)write_played(written, note, 120);
    const auto events = written.events();
    const auto kept = std::count_if(events.begin(), events.end(), [](const Trigger& trigger) {
        return !trigger.expression.empty();
    });
    require(kept == 1, "written into the pattern, the expression stays with its note's step");
}

// A long A4 on the CLAP fixture: pressure 1.0 at 250 ticks into it, then
// +12 semitones at 500.
Song expressive_song(bool tone = false) {
    Song song;
    Pattern pattern(1920, 480);
    Trigger note;
    note.start = 0;
    note.duration = 1900;
    note.musical_data = Note{69, 1.0F, 0.0F, 0.0};
    note.expression = {{0, 250, NoteExpression::Kind::pressure, 1.0F},
                       {0, 500, NoteExpression::Kind::pitch, 12.0F}};
    if (tone) note.locks = {{"tone", 1, 1.0, ParameterLock::Kind::automation}};
    (void)pattern.add(note);
    song.patterns = {{"P", std::move(pattern)}};
    song.tracks = {Track{}};
    song.clips = {{0, 0, 0, 1}};
    return song;
}

// features/continuous_midi.feature:
//   Scenario: Recorded per-note expression plays back to CLAP as note expressions, on its sample
void mpe_clap_playback() {
    auto song = expressive_song();
    std::string why;
    require(song.consistent(&why), "valid: " + why);
    SongEngine engine;
    engine.set_instrument(0, clap());
    require(engine.prepare(song, rate, block, 0, &why), "prepare: " + why);
    engine.set_playing(true);
    (void)clap_expressions();
    const auto out = render_engine(engine, 30000);
    require(close_to(out[12499], clap_level) && close_to(out[12500], 2.0F * clap_level),
            "the note's pressure lands on its sample: " + std::to_string(out[12499]) + " / " +
                std::to_string(out[12500]));
    const auto seen = clap_expressions();
    const bool pressure_at = std::any_of(seen.begin(), seen.end(), [](const ExpressionSeen& e) {
        return e.id == clap_pressure && e.key == 69 && e.value == 1.0 && e.time == 12500 % block;
    });
    const bool tuning_at = std::any_of(seen.begin(), seen.end(), [](const ExpressionSeen& e) {
        return e.id == clap_tuning && e.key == 69 && e.value == 12.0 && e.time == 25000 % block;
    });
    require(pressure_at && tuning_at,
            "CLAP gets CLAP_EVENT_NOTE_EXPRESSION pressure and tuning at their offsets in the block");

    // A chord: only voice 1 is pressed, in the fixture's velocity mode (the
    // sum of the held keys' velocities, each times its own pressure).
    Song chord_song;
    Pattern chord_pattern(1920, 480);
    Trigger chord;
    chord.start = 0;
    chord.duration = 1900;
    chord.musical_data = Chord{60, {0, 7}, 0, 0, {}, 0.5F};
    chord.locks = {{"velocity-mode", 2, 1.0, ParameterLock::Kind::automation}};
    chord.expression = {{1, 100, NoteExpression::Kind::pressure, 1.0F}};
    (void)chord_pattern.add(chord);
    chord_song.patterns = {{"C", std::move(chord_pattern)}};
    chord_song.tracks = {Track{}};
    chord_song.clips = {{0, 0, 0, 1}};
    SongEngine chords;
    chords.set_instrument(0, clap());
    require(chords.prepare(chord_song, rate, block, 0, &why), "prepare: " + why);
    chords.set_playing(true);
    const auto voiced = render_engine(chords, 10000);
    require(close_to(voiced[4999], clap_level * 1.0F) && close_to(voiced[5000], clap_level * 1.5F),
            "only the chord's second voice is pressed, from its sample: " +
                std::to_string(voiced[4999]) + " / " + std::to_string(voiced[5000]));

    // Launched, the pattern plays its expression as the arrangement does.
    auto launched = song;
    launched.launcher.add_scene("S");
    launched.launcher.quantization = LaunchQuantization::none;
    launched.launcher.set_slot(0, 0, {.pattern = 0});
    SongEngine launcher;
    launcher.set_instrument(0, clap());
    require(launcher.prepare(launched, rate, block, 0, &why), "prepare: " + why);
    require(launcher.launch_cell(0, 0), "launched");
    launcher.set_playing(true);
    const auto from_launcher = render_engine(launcher, 30000);
    std::size_t differing = 0;
    for (std::size_t at = 0; at < out.size(); ++at)
        if (!close_to(from_launcher[at], out[at], 1e-7F)) ++differing;
    require(differing == 0, "a launched loop plays the note's expression as the arrangement "
                            "does; " + std::to_string(differing) + " samples differ");

    // Exported: read back, as the engine plays it.
    const fs::path file = fs::path(BLOKKILY_CONTINUOUS_WORK) / "mpe-clap.wav";
    fs::create_directories(file.parent_path());
    std::string error;
    require(bounce_song(engine, file, WaveFormat::float32, 0, &error).has_value(),
            "the bounce is written: " + error);
    const auto wave = read_wave(file, &error);
    require(wave.has_value() && wave->frames >= 30000, "the bounce reads back: " + error);
    double worst = 0.0;
    for (std::size_t frame = 0; frame < 30000; ++frame)
        worst = std::max(worst, static_cast<double>(std::abs(wave->interleaved[2 * frame] - out[frame])));
    require(worst < 1e-6, "the export is the live render, worst " + std::to_string(worst));
}

// features/continuous_midi.feature:
//   Scenario: A SoundFont bends one note of a chord through its own channel
void mpe_soundfont() {
    const auto chord_with = [](std::vector<NoteExpression> expression) {
        Song song;
        Pattern pattern(1920, 480);
        Trigger chord;
        chord.start = 0;
        chord.duration = 1900;
        chord.musical_data = Chord{69, {0, 7}, 0, 0, {}, 0.8F};   // A4 and E5
        chord.expression = std::move(expression);
        (void)pattern.add(chord);
        song.patterns = {{"P", std::move(pattern)}};
        song.tracks = {Track{}};
        song.clips = {{0, 0, 0, 1}};
        return song;
    };
    const auto render_chord = [](const Song& song) {
        SongEngine engine;
        engine.set_instrument(0, soundfont());
        std::string why;
        require(engine.prepare(song, rate, block, 0, &why), "prepare: " + why);
        engine.set_playing(true);
        return render_engine(engine, 40000);
    };
    const auto plain = render_chord(chord_with({}));
    // Voice 0 (A4) bent up two semitones from its start; E5 is not.
    const auto bent = render_chord(chord_with({{0, 0, NoteExpression::Kind::pitch, 2.0F}}));
    require_frequency(frequency(plain, 8000, 16384, 400, 520), a4, "the plain chord's A4");
    require_frequency(frequency(bent, 8000, 16384, 400, 520), a4_bent_up,
                      "the expressive voice is bent on its own channel");
    // The SoundFont's own E5 sample sits where it sits; the other voice
    // must sit exactly there with or without the expressive one bent.
    const double e5 = frequency(plain, 8000, 16384, 600, 720);
    require(std::abs(e5 - 440.0 * std::pow(2.0, 7.0 / 12.0)) < 12.0, "the plain chord's E5");
    require_frequency(frequency(bent, 8000, 16384, 600, 720), e5,
                      "while the chord's other voice keeps its pitch");
}

// features/continuous_midi.feature:
//   Scenario: A VST3 instrument hears per-note expression on the note's own channel
void mpe_vst3() {
    const auto render_note = [](std::vector<NoteExpression> expression, bool tone) {
        Song song;
        Pattern pattern(1920, 480);
        Trigger note;
        note.start = 0;
        note.duration = 1900;
        note.musical_data = Note{69, 1.0F, 0.0F, 0.0};
        note.expression = std::move(expression);
        (void)pattern.add(note);
        song.patterns = {{"P", std::move(pattern)}};
        song.tracks = {Track{}};
        song.clips = {{0, 0, 0, 1}};
        Rig rig(vst3(), song);
        if (tone) rig.play({PluginEvent::Type::parameter_value, 0, 1, 1.0});
        rig.engine.set_playing(true);
        return rig.render(40000);
    };
    // Pitch: one semitone up from the start, on the note's own wheel.
    const auto sharp = render_note({{0, 0, NoteExpression::Kind::pitch, 1.0F}}, true);
    require_frequency(frequency(sharp, 8000, 16384, 400, 520), 440.0 * std::pow(2.0, 1.0 / 12.0),
                      "a VST3 note bent by its expression");
    // Beyond the channel's two-semitone range the bend stops at its edge.
    const auto wide = render_note({{0, 0, NoteExpression::Kind::pitch, 7.0F}}, true);
    require_frequency(frequency(wide, 8000, 16384, 400, 620), a4_bent_up,
                      "a VST3 note's expression is clamped to the two-semitone voice channel");
    // Pressure at tick 400 (sample 20000): the fixture's level doubles there.
    const auto pressed = render_note({{0, 400, NoteExpression::Kind::pressure, 1.0F}}, false);
    const float before = pressed[19000];
    const float after = pressed[21000];
    require(before > 0.05F && std::abs(after - 2.0F * before) < 1e-3F,
            "a VST3 note's pressure reaches it as its channel's pressure: " +
                std::to_string(before) + " -> " + std::to_string(after));
}

// features/continuous_midi.feature:
//   Scenario: Per-note expression is saved with its note
void mpe_records() {
    auto song = expressive_song();
    Project project;
    project.song = song;
    const auto text = ProjectFile::serialize(project);
    require(text.find("express 0 1 0 250 pressure 1\n") != std::string::npos &&
                text.find("express 0 1 0 500 pitch 12\n") != std::string::npos,
            "each value is saved as an express record:\n" + text);
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value() && ProjectFile::serialize(*loaded) == text &&
                loaded->song.patterns[0].pattern.events()[0].expression ==
                    song.patterns[0].pattern.events()[0].expression,
            "and reads back byte for byte: " + error);
    std::string older;
    for (std::size_t from = 0; from < text.size();) {
        const auto end = text.find('\n', from);
        const auto line = text.substr(from, end - from + 1);
        if (line.rfind("express ", 0) != 0) older += line;
        from = end + 1;
    }
    const auto old = ProjectFile::parse(older, &error);
    require(old.has_value() && old->song.patterns[0].pattern.events()[0].expression.empty(),
            "a file from before MPE loads with no expression: " + error);
    for (const char* bad : {"express 0 1 1 10 pitch 1\n", "express 0 1 0 -1 pitch 1\n",
                            "express 0 1 0 10 wiggle 1\n", "express 0 1 0 10 pitch 97\n",
                            "express 0 1 0 10 pressure 1.5\n", "express 0 9 0 10 pitch 1\n",
                            "express 3 1 0 10 pitch 1\n", "express 0 1 0 10 pitch\n"})
        require(!ProjectFile::parse(text + bad, &error).has_value(),
                std::string("a malformed express record is refused: ") + bad);

    // A played note merged onto a step that already sounds another pitch:
    // the chord keeps each voice's expression on that voice.
    Pattern pattern(1920, 480);
    PlayedNote high{0, 400, 72, 0.7F, 0.0, {{0, 30, NoteExpression::Kind::pressure, 0.25F}}};
    PlayedNote low{5, 400, 60, 0.9F, 0.0, {{0, 60, NoteExpression::Kind::pitch, -2.0F}}};
    (void)write_played(pattern, high, 120);
    (void)write_played(pattern, low, 120);
    require(pattern.events().size() == 1, "one step");
    const auto& merged = pattern.events()[0];
    const auto* chord = std::get_if<Chord>(&merged.musical_data);
    require(chord != nullptr && chord->root == 60 && merged.expression.size() == 2,
            "a chord of both, with both notes' expression");
    for (const auto& point : merged.expression) {
        if (point.kind == NoteExpression::Kind::pitch)
            require(point.voice == 0 && point.offset == 60, "the low voice keeps its bend");
        else
            require(point.voice == 1 && point.offset == 30, "the high voice keeps its pressure");
    }
    Trigger bad;
    bad.expression = {{1, 0, NoteExpression::Kind::pitch, 1.0F}};
    bool refused = false;
    try {
        (void)pattern.add(bad);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "the pattern refuses expression for a voice a note does not have");
}

} // namespace

int main(int argc, char** argv) {
    const std::map<std::string, std::function<void()>> cases{
        {"soundfont_bend", soundfont_bend},
        {"soundfont_sustain", soundfont_sustain},
        {"vst3_bend", vst3_bend},
        {"sampler_bend_sustain", sampler_bend_sustain},
        {"pattern_sample_accurate", pattern_sample_accurate},
        {"chase", chase},
        {"record_take", record_take},
        {"export_matches", export_matches},
        {"poly_pressure", poly_pressure},
        {"mpe_clap_live", mpe_clap_live},
        {"mpe_clap_playback", mpe_clap_playback},
        {"mpe_soundfont", mpe_soundfont},
        {"mpe_vst3", mpe_vst3},
        {"mpe_records", mpe_records},
    };
    if (argc < 2 || !cases.contains(argv[1])) {
        std::cerr << "Usage: blokkily_continuous_midi_tests <case>\n";
        return 2;
    }
    try {
        cases.at(argv[1])();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << argv[1] << " passed\n";
    return 0;
}
