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
