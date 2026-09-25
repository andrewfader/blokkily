// The sampler's DSP core (design 2, plan item 1.9), proved from the audio that
// SamplerInstrument::process renders from the generated audio fixtures,
// decoded through the shared AudioAssetCache. No claim here rests on a zone's
// fields or a flag: every pitch, level, envelope, loop and slice is measured
// in the rendered output.
//
// Run with a case name; each case is its own CTest test (sampler_<case>).
// features/sampler.feature maps its scenarios to these cases.

#include "fixtures/audio_fixture_content.hpp"
#include "support/audio_probe.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/instruments/sampler.hpp"
#include "blokkily/model/tuning.hpp"
#include "blokkily/project/project.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace blokkily;
namespace fx = blokkily::audio_fixtures;

namespace {

const std::filesystem::path fixtures = BLOKKILY_AUDIO_FIXTURES;
constexpr double rate = 48000.0;
constexpr std::size_t block = 256;

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

std::string number(double value) { return std::to_string(value); }

std::string fixture(const char* name) { return (fixtures / name).string(); }

struct Timed {
    std::size_t at;
    PluginEvent event;
};

Timed note_on(std::size_t at, int key, double velocity = 1.0, double cents = 0.0) {
    return {at, {PluginEvent::Type::note_on, 0, key, velocity, cents}};
}
Timed note_off(std::size_t at, int key) {
    return {at, {PluginEvent::Type::note_off, 0, key, 0.0}};
}
Timed set_parameter(std::size_t at, std::int32_t id, double value) {
    return {at, {PluginEvent::Type::parameter_value, 0, id, value}};
}
Timed modulate(std::size_t at, std::int32_t id, double value) {
    return {at, {PluginEvent::Type::parameter_modulation, 0, id, value}};
}

struct Audio {
    std::vector<float> left, right;
    [[nodiscard]] std::span<const float> window(std::size_t from, std::size_t length) const {
        return std::span<const float>(left).subspan(from, length);
    }
};

// Renders `frames` through process() in blocks, placing each event at its
// sample offset inside the block it falls in.
Audio render(SamplerInstrument& sampler, std::size_t frames, std::vector<Timed> events) {
    std::stable_sort(events.begin(), events.end(),
                     [](const Timed& a, const Timed& b) { return a.at < b.at; });
    Audio out{std::vector<float>(frames, -7.0F), std::vector<float>(frames, -7.0F)};
    std::vector<PluginEvent> due;
    std::size_t next = 0;
    for (std::size_t at = 0; at < frames; at += block) {
        const std::size_t length = std::min(block, frames - at);
        due.clear();
        while (next < events.size() && events[next].at < at + length) {
            PluginEvent event = events[next].event;
            event.sample_offset = static_cast<std::uint32_t>(events[next].at - at);
            due.push_back(event);
            ++next;
        }
        sampler.process({std::span<float>(out.left.data() + at, length),
                         std::span<float>(out.right.data() + at, length)},
                        due);
    }
    return out;
}

SamplerZone zone_for(const std::string& file) {
    SamplerZone zone;
    zone.sample = file;
    zone.envelope.attack_s = 0.0;
    zone.envelope.release_s = 0.0;
    return zone;
}

std::unique_ptr<SamplerInstrument> sampler_with(const SamplerProgram& program,
                                                AudioAssetCache* cache = nullptr) {
    auto sampler = std::make_unique<SamplerInstrument>(cache);
    require(sampler->activate(rate, 1, block), "the sampler must activate");
    std::string error;
    require(sampler->set_program(program, &error), "set_program: " + error);
    require(sampler->missing_samples().empty(),
            "every fixture must load: " +
                (sampler->missing_samples().empty() ? std::string{}
                                                    : sampler->missing_samples().front()));
    return sampler;
}

SamplerProgram keyed(std::vector<SamplerZone> zones) {
    SamplerProgram program = default_sampler(SamplerProgram::Mode::keyed);
    program.zones = std::move(zones);
    return program;
}

double frequency_near(std::span<const float> samples, double expected, double span,
                      double step = 0.05) {
    return probe::dominant_frequency(samples, rate, expected - span, expected + span, step);
}

// The sine1k fixture (1 kHz) as a zone whose root and fine tuning make it
// sound exactly the pitch the engine asks for: 1 kHz is 83 + 21.3 cents.
SamplerZone tuned_sine() {
    SamplerZone zone = zone_for(fixture("sine1k_44k1_pcm16.wav"));
    const double semitones = 12.0 * std::log2(1000.0 / 440.0);   // above A4 = 69
    zone.root_key = 69 + static_cast<int>(std::floor(semitones));
    zone.fine_cents = (semitones - std::floor(semitones)) * 100.0;
    return zone;
}

// A constant 0.5 (the step fixture after its step), so the output divided by
// 0.5 is the voice's gain times its envelope, sample by sample.
SamplerZone dc_zone() {
    SamplerZone zone = zone_for(fixture("step_44k1_float.wav"));
    zone.start = fx::step_frame + 10;
    zone.low_key = zone.high_key = zone.root_key = 60;
    return zone;
}

double mean(std::span<const float> samples) {
    double sum = 0.0;
    for (const float sample : samples) sum += sample;
    return sum / static_cast<double>(samples.size());
}

double max_step(std::span<const float> samples) {
    double largest = 0.0;
    for (std::size_t i = 1; i < samples.size(); ++i)
        largest = std::max(largest, std::abs(static_cast<double>(samples[i]) - samples[i - 1]));
    return largest;
}

double gain_at(double parameter_value) {
    return std::pow(10.0, (-60.0 + 72.0 * parameter_value) / 20.0);
}

std::string text_of(const std::vector<std::byte>& bytes) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

std::vector<std::byte> bytes_of(const std::string& text) {
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

// ------------------------------------------------------------------ cases --

// A zone made from a file starts from the file's own sampler metadata: the
// smpl chunk's unity note and its sustain loop.
void file_metadata() {
    SamplerInstrument probe_instrument;
    std::string error;
    const auto zone = probe_instrument.make_zone(fixture("smpl_loop_44k1_pcm16.wav"), &error);
    require(zone.has_value(), "make_zone: " + error);
    require(zone->root_key == fx::smpl_root_key, "the smpl unity note must become the root key");
    require(zone->loop == LoopMode::forward && zone->loop_start == fx::smpl_loop_first &&
                zone->loop_end == fx::smpl_loop_last + 1,
            "the smpl loop must become the zone's forward loop, end exclusive: " +
                std::to_string(zone->loop_start) + ".." + std::to_string(zone->loop_end));

    auto sampler = sampler_with(keyed({*zone}));
    // Three times the file's length at its root: 4410 frames at 44.1 kHz.
    const std::size_t held = 3 * 4800;
    const auto audio = render(*sampler, held + 480, {note_on(0, fx::smpl_root_key)});
    // Measured before the first loop turn: the loop itself is 19.95 cycles
    // long, so the sustained part is a 2000-frame period, not a pure 440 Hz.
    const double hz = frequency_near(audio.window(200, 3000), 440.0, 20.0);
    require(std::abs(hz - 440.0) <= 2.0, "the root key must play the file's 440 Hz: " + number(hz));
    require(probe::rms(audio.window(held, 480)) > 0.1,
            "the file's own loop must sustain the note past three times its length");
}

// A zone whose file is missing or unreadable is kept and reported; the rest
// of the program still plays.
void missing_sample() {
    SamplerZone gone = zone_for(fixture("no_such_sample.wav"));
    gone.low_key = 0;
    gone.high_key = 40;
    SamplerZone garbage = zone_for(fixture("garbage.wav"));
    garbage.low_key = 41;
    garbage.high_key = 59;
    SamplerZone good = tuned_sine();
    good.low_key = 60;
    good.high_key = 127;
    const SamplerProgram program = keyed({gone, garbage, good});

    SamplerInstrument sampler;
    require(sampler.activate(rate, 1, block), "activate");
    std::string error;
    require(sampler.set_program(program, &error), "a missing file must not fail the program");
    require(sampler.missing_samples().size() == 2, "both broken zones must be reported");
    require(sampler.missing_samples()[0].find("no_such_sample.wav") != std::string::npos &&
                sampler.missing_samples()[1].find("garbage.wav") != std::string::npos,
            "each report must name its file: " + sampler.missing_samples()[0]);
    require(sampler.program() == program, "the broken zones stay in the program");
    const auto reloaded = parse_sampler(sampler.save_state());
    require(reloaded && reloaded->zones.size() == 3, "the broken zones round-trip");

    const auto silent = render(sampler, 4800, {note_on(0, 30), note_on(0, 50)});
    require(probe::rms(silent.left) == 0.0, "notes on broken zones must be silent");
    const auto sounding = render(sampler, 4800, {note_on(0, 69)});
    require(probe::rms(sounding.window(480, 4000)) > 0.1, "the good zone still plays");
}

// Keyed pitch: every note sounds at the pitch the engine asked for, including
// cents and a 19-EDO degree, through a 44.1 kHz file played at 48 kHz.
void keyed_pitch() {
    // A fresh sampler for every note, so no earlier voice is still sounding.
    const auto measure = [](int key, double cents, double expected) {
        auto sampler = sampler_with(keyed({tuned_sine()}));
        const auto audio = render(*sampler, 24000, {note_on(0, key, 1.0, cents)});
        return frequency_near(audio.window(2400, 19200), expected, 30.0);
    };
    const double a4 = measure(69, 0.0, 440.0);
    require(std::abs(a4 - 440.0) <= 1.0, "note 69 must sound 440 Hz: " + number(a4));
    const double a5 = measure(81, 0.0, 880.0);
    require(std::abs(a5 - 880.0) <= 2.0, "note 81 must sound 880 Hz: " + number(a5));
    const double retuned = measure(69, 50.0, 452.893);
    require(std::abs(retuned - 452.893) <= 1.0,
            "note 69 + 50 cents must sound 452.9 Hz: " + number(retuned));

    const Tuning nineteen = equal_division(19);
    const TunedPitch degree = degree_pitch(nineteen, 5);
    require(degree.cents != 0.0, "the 19-EDO degree must need a retune");
    const double expected = pitch_frequency(degree);
    const double heard = measure(degree.key, degree.cents, expected);
    require(std::abs(heard - expected) <= 1.0, "a 19-EDO degree must sound " + number(expected) +
                                                   " Hz, not " + number(heard));
}

// Keys and velocities outside a zone are silent; velocity layers choose
// different zones.
void key_velocity_range() {
    SamplerZone zone = tuned_sine();
    zone.low_key = 48;
    zone.high_key = 72;
    auto sampler = sampler_with(keyed({zone}));
    const auto below = render(*sampler, 4800, {note_on(0, 47)});
    require(probe::rms(below.left) < 1e-7, "a key below the zone must be silent");
    const auto above = render(*sampler, 4800, {note_on(0, 73)});
    require(probe::rms(above.left) < 1e-7, "a key above the zone must be silent");
    const auto inside = render(*sampler, 4800, {note_on(0, 60)});
    require(probe::rms(inside.window(480, 4000)) > 1e-2, "a key inside the zone must sound");

    SamplerZone soft = tuned_sine();
    soft.high_velocity = 63;
    SamplerInstrument probe_instrument;
    auto hard = *probe_instrument.make_zone(fixture("loop480_48k_pcm16.wav"));
    hard.envelope.attack_s = 0.0;
    hard.low_velocity = 64;
    // Note 71 is the loop's root (480 Hz) and sounds the sine zone at
    // 440 * 2^(2/12) = 493.9 Hz.
    const auto quiet = render(*sampler_with(keyed({soft, hard})), 9600, {note_on(0, 71, 0.3)});
    const auto loud = render(*sampler_with(keyed({soft, hard})), 9600, {note_on(0, 71, 0.9)});
    const double quiet_hz = frequency_near(quiet.window(480, 8192), 487.0, 30.0);
    const double loud_hz = frequency_near(loud.window(480, 8192), 487.0, 30.0);
    require(std::abs(quiet_hz - 493.88) <= 2.0,
            "velocity 38 must play the soft layer: " + number(quiet_hz));
    require(std::abs(loud_hz - 480.0) <= 2.0, "velocity 114 must play the hard layer: " +
                                                  number(loud_hz));
}

// The level a velocity plays at follows the documented (linear) curve.
void velocity() {
    const auto full = render(*sampler_with(keyed({tuned_sine()})), 9600, {note_on(0, 69, 1.0)});
    const auto half = render(*sampler_with(keyed({tuned_sine()})), 9600, {note_on(0, 69, 0.5)});
    const double ratio = probe::rms(full.window(480, 9000)) / probe::rms(half.window(480, 9000));
    const double expected = sampler_velocity_gain(1.0) / sampler_velocity_gain(0.5);
    require(std::abs(ratio / expected - 1.0) <= 0.05,
            "RMS(v=1)/RMS(v=0.5) must be " + number(expected) + ", was " + number(ratio));
}

// ADSR, measured on a constant signal so the output is the envelope itself.
void envelope() {
    SamplerZone zone = dc_zone();
    zone.envelope = {0.010, 0.020, 0.5, 0.050};
    auto sampler = sampler_with(keyed({zone}));
    const std::size_t release_at = 14400;   // 300 ms
    const auto audio = render(*sampler, 24000, {note_on(0, 60), note_off(release_at, 60)});
    const auto env = [&](std::size_t frame) { return audio.left[frame] / 0.5; };

    std::size_t ninety = 0, full = 0;
    while (ninety < audio.left.size() && env(ninety) < 0.9) ++ninety;
    while (full < audio.left.size() && env(full) < 0.999) ++full;
    require(ninety <= 480, "the attack must reach 90% by 10 ms (" + std::to_string(ninety) + ")");
    require(full >= 432 && full <= 528,
            "the attack must peak at 10 +/- 1 ms, not " + std::to_string(full) + " samples");
    require(env(0) < 0.01, "the attack must start from silence");
    for (std::size_t frame = 2400; frame < release_at; ++frame)
        require(std::abs(env(frame) - 0.5) <= 0.015,
                "the sustain must hold at half (+/-3%) at " + std::to_string(frame) + ": " +
                    number(env(frame)));
    const double mid_release = env(release_at + 1200);   // 25 ms into a 50 ms release
    require(mid_release > 0.2 && mid_release < 0.3,
            "the release must fall linearly, not cut: " + number(mid_release));
    require(probe::rms(audio.window(release_at + 2880, 480)) < 1e-4,
            "the note must be silent 60 ms after the release");

    // One key-addressed note_off releases every voice of that key.
    const auto stacked = render(*sampler, 24000,
                                {note_on(0, 60), note_on(100, 60), note_off(release_at, 60)});
    require(std::abs(stacked.left[release_at - 10] - 0.5) < 0.01,
            "two stacked voices sound together: " + number(stacked.left[release_at - 10]));
    require(probe::rms(stacked.window(release_at + 2880, 480)) < 1e-4,
            "one note_off must release both stacked voices");
    const auto held = render(*sampler, 24000, {note_on(0, 60), note_on(100, 60)});
    require(probe::rms(held.window(release_at + 2880, 480)) > 0.4,
            "without the note_off the stacked voices keep sounding");
}

// Loops: a forward and a ping-pong loop sustain a held note; with the loop off
// the file runs out. The seam adds no step larger than the sine's own.
void loop() {
    SamplerInstrument probe_instrument;
    auto zone = *probe_instrument.make_zone(fixture("loop480_48k_pcm16.wav"));
    require(zone.root_key == fx::loop480_root_key && zone.loop == LoopMode::forward &&
                zone.loop_start == fx::loop480_loop_first &&
                zone.loop_end == fx::loop480_loop_last + 1,
            "the loop fixture's smpl metadata must be read");
    const std::size_t held = 3 * fx::loop480_frames;

    // The steepest step of a 0.5 sine at 480 Hz one semitone up, plus 10%.
    const double sine_step = 0.5 * fx::two_pi * 480.0 * std::exp2(1.0 / 12.0) / rate;
    for (const auto mode : {LoopMode::forward, LoopMode::ping_pong}) {
        zone.loop = mode;
        auto sampler = sampler_with(keyed({zone}));
        const auto audio = render(*sampler, held + 480, {note_on(0, 72)});
        const char* name = mode == LoopMode::forward ? "forward" : "ping-pong";
        require(probe::rms(audio.window(held, 480)) > 0.3,
                std::string("a ") + name + " loop must still sound after 3x the file length");
        // A ping-pong turn is a corner, which the interpolator rounds a
        // little; a real seam discontinuity would be many times larger.
        const double allowed = sine_step * (mode == LoopMode::forward ? 1.1 : 1.2);
        const double largest = max_step(audio.window(200, held));
        require(largest <= allowed, std::string("the ") + name +
                                                " loop seam must be continuous: step " +
                                                number(largest) + " vs " + number(allowed));
    }
    zone.loop = LoopMode::off;
    auto one_pass = sampler_with(keyed({zone}));
    const auto audio = render(*one_pass, held + 480, {note_on(0, 71)});
    require(probe::rms(audio.window(1000, 480)) > 0.3, "the unlooped file plays");
    require(probe::rms(audio.window(held, 480)) == 0.0,
            "with the loop off the note ends with the file");
}

// Regression: once a forward loop has wrapped, the interpolator's taps on the
// near side of loop_start must read the loop's end, not the file before the
// loop. The loopseam fixture's lead-in (0.9) is nothing like the loop's last
// frame, so a tap that reads it clicks at every turn; the loop itself is whole
// cycles of a sine, so a tap that reads the loop is as smooth as the sine.
void loop_seam() {
    SamplerInstrument probe_instrument;
    auto zone = *probe_instrument.make_zone(fixture("loopseam_48k_pcm16.wav"));
    require(zone.loop == LoopMode::forward && zone.loop_start == fx::loop480_loop_first &&
                zone.loop_end == fx::loop480_loop_last + 1,
            "the seam fixture's smpl loop must be read");
    // One semitone up, so the read position is fractional and every output
    // sample is interpolated from four taps.
    const double ratio = std::exp2(1.0 / 12.0);
    const double sine_step = 0.5 * fx::two_pi * 480.0 * ratio / rate;
    // The voice reaches loop_end after this many output frames; everything
    // after it is loop, turn after turn.
    const auto first_turn =
        static_cast<std::size_t>(std::ceil(static_cast<double>(zone.loop_end) / ratio));
    const std::size_t held = first_turn + 4 * fx::loop480_frames;
    auto sampler = sampler_with(keyed({zone}));
    const auto audio = render(*sampler, held, {note_on(0, 72)});
    const auto looped = audio.window(first_turn + 2, held - first_turn - 2);
    require(probe::rms(looped) > 0.3, "the loop sustains the note");
    const double largest = max_step(looped);
    require(largest <= sine_step * 1.1,
            "every turn of the loop must be continuous: largest step " + number(largest) +
                " vs the sine's own " + number(sine_step));
    // And the loop sounds as the loop does in the smooth fixture, sample for
    // sample: the lead-in is never heard once the voice has wrapped.
    auto smooth_zone = *probe_instrument.make_zone(fixture("loop480_48k_pcm16.wav"));
    auto smooth = sampler_with(keyed({smooth_zone}));
    const auto reference = render(*smooth, held, {note_on(0, 72)});
    double worst = 0.0;
    for (std::size_t frame = first_turn + 2; frame < held; ++frame)
        worst = std::max(worst, std::abs(static_cast<double>(audio.left[frame]) -
                                         reference.left[frame]));
    require(worst < 1e-6, "after the first turn the seam fixture must render exactly as the "
                          "smooth loop does: differs by " + number(worst));
}

// A loop chopped into eight slices plays hit i on key 36 + i, sample-exact.
void kit_slices() {
    AudioAssetCache cache;
    SamplerInstrument probe_instrument(&cache);
    const auto source_file = fixture("tones8_48k_pcm16.wav");
    SamplerZone source = zone_for(source_file);
    const auto asset = probe_instrument.asset_for(source);
    require(asset && asset->frames == fx::tones8_frames, "the slice source must decode");

    SamplerProgram program = default_sampler(SamplerProgram::Mode::keyed);
    require(slice_evenly(program, source, asset->frames, 8, 36), "slice_evenly");
    require(program.mode == SamplerProgram::Mode::kit && program.zones.size() == 8,
            "slicing makes an eight-zone kit");
    for (std::size_t i = 0; i < 8; ++i) {
        const auto& slice = program.zones[i];
        require(slice.start == i * fx::tones8_spacing && slice.end == (i + 1) * fx::tones8_spacing,
                "slice " + std::to_string(i) + " boundaries must be exact");
        require(slice.low_key == 36 + static_cast<int>(i) && slice.high_key == slice.low_key &&
                    !slice.track_pitch && slice.one_shot,
                "slice " + std::to_string(i) + " must be a one-shot pad on its own key");
    }

    auto sampler = sampler_with(program, &cache);
    for (std::size_t i = 0; i < 8; ++i) {
        const int key = 36 + static_cast<int>(i);
        const auto audio = render(*sampler, 8000, {note_on(0, key), note_off(3000, key)});
        const double hz = probe::dominant_frequency(audio.window(0, fx::tones8_hit_length), rate,
                                                    100.0, 1800.0, 1.0);
        require(std::abs(hz - fx::tones8_hz(i)) <= 5.0,
                "pad " + std::to_string(key) + " must play hit " + std::to_string(i) + " at " +
                    number(fx::tones8_hz(i)) + " Hz, not " + number(hz));
        for (std::size_t n = 0; n < fx::tones8_spacing; ++n)
            require(std::abs(audio.left[n] - asset->left[i * fx::tones8_spacing + n]) < 1e-6F,
                    "pad " + std::to_string(key) + " must play its slice sample-exact at " +
                        std::to_string(n));
        require(probe::peak(audio.window(fx::tones8_spacing, 2000)) == 0.0F,
                "pad " + std::to_string(key) + " must stop at its slice's end");
    }
    require(!slice_evenly(program, source, asset->frames, 0, 36) &&
                !slice_evenly(program, source, asset->frames, 8, 125),
            "impossible slicings are refused");
}

// A one-shot ignores its note_off; a choke group cuts a ringing pad within
// sampler_fast_release_frames.
void one_shot_and_choke() {
    SamplerZone hit = zone_for(fixture("tones8_48k_pcm16.wav"));
    hit.end = fx::tones8_hit_length;
    hit.one_shot = true;
    hit.track_pitch = false;
    hit.low_key = hit.high_key = 36;
    auto one_shot = sampler_with(keyed({hit}));
    const auto whole = render(*one_shot, 2000, {note_on(0, 36), note_off(100, 36)});
    const auto reference = render(*one_shot, 2000, {note_on(0, 36)});
    require(whole.left == reference.left, "a one-shot must ignore its note_off");
    require(probe::rms(whole.window(1000, 400)) > 0.1, "the one-shot plays past the note_off");

    SamplerInstrument probe_instrument;
    auto ring = *probe_instrument.make_zone(fixture("loop480_48k_pcm16.wav"));
    ring.envelope.attack_s = 0.0;
    ring.low_key = ring.high_key = ring.root_key = 46;
    ring.one_shot = true;
    ring.choke_group = 1;
    SamplerZone closed = zone_for(fixture("tones8_48k_pcm16.wav"));
    closed.start = 2000;   // a silent stretch: only the choke can change the output
    closed.end = 5000;
    closed.low_key = closed.high_key = 42;
    closed.track_pitch = false;
    closed.one_shot = true;
    closed.choke_group = 1;
    SamplerProgram kit = default_sampler(SamplerProgram::Mode::kit);
    kit.zones = {ring, closed};
    auto choking = sampler_with(kit);
    const std::size_t strike = 4800;
    const auto audio = render(*choking, 9600, {note_on(0, 46), note_on(strike, 42)});
    require(probe::rms(audio.window(strike - 480, 480)) > 0.3, "pad 46 rings before the strike");
    require(probe::peak(audio.window(strike + sampler_fast_release_frames + 1,
                                     9600 - strike - sampler_fast_release_frames - 1)) == 0.0F,
            "pad 42 must cut pad 46 within 64 samples");
    require(probe::peak(audio.window(strike, 16)) > 0.0F, "the cut is a fade, not a click");

    kit.zones[1].choke_group = 2;
    auto separate = sampler_with(kit);
    const auto both = render(*separate, 9600, {note_on(0, 46), note_on(strike, 42)});
    require(probe::rms(both.window(9000, 480)) > 0.3,
            "pads in different groups do not cut each other");
}

// State: a saved program reloads into a new sampler that renders the same
// audio, bit for bit; the blob and a project holding it round-trip exactly.
void state() {
    SamplerZone low = tuned_sine();
    low.high_key = 64;
    low.envelope = {0.004, 0.03, 0.7, 0.02};
    low.gain_db = -3.5;
    low.pan = -0.25;
    low.fine_cents += 0.1;
    SamplerInstrument probe_instrument;
    auto high = *probe_instrument.make_zone(fixture("loop480_48k_pcm16.wav"));
    high.loop = LoopMode::ping_pong;
    high.low_key = 65;
    high.low_velocity = 20;
    high.choke_group = 3;
    SamplerProgram program = keyed({low, high});
    program.polyphony = 12;

    const std::vector<Timed> events{note_on(0, 60, 0.8), note_on(300, 71, 0.6, 12.5),
                                    set_parameter(900, sampler_parameter::gain, 0.7),
                                    modulate(1500, sampler_parameter::tune, 0.02),
                                    note_off(4000, 60), note_off(6000, 71)};
    auto first = sampler_with(program);
    const auto saved = first->save_state();
    const auto original = render(*first, 12000, events);

    SamplerInstrument second;
    require(second.activate(rate, 1, block), "activate");
    require(second.load_state(saved), "a saved state must load");
    require(second.program() == program, "the loaded program equals the saved one");
    const auto reloaded = render(second, 12000, events);
    require(original.left == reloaded.left && original.right == reloaded.right,
            "the reloaded sampler must render bit-identical audio");
    require(probe::rms(original.left) > 0.01, "and that audio is not silence");

    const auto parsed = parse_sampler(saved);
    require(parsed && serialize_sampler(*parsed) == saved, "serialize(parse(x)) == x");
    require(second.save_state() == saved, "save, load, save gives the same bytes");

    Project project;
    Track track;
    track.name = "Keys";
    track.instrument = {"Sampler", "", "blokkily.sampler", saved};
    project.song.tracks = {track};
    const auto text = ProjectFile::serialize(project);
    std::string error;
    const auto loaded = ProjectFile::parse(text, &error);
    require(loaded.has_value(), "project parse: " + error);
    require(loaded->song.tracks.size() == 1 && loaded->song.tracks[0].instrument.state == saved,
            "a project keeps the sampler's state");
    require(ProjectFile::serialize(*loaded) == text, "a project with a sampler round-trips");

    // Anything but a version-1 blob of valid records is refused whole.
    const std::string good = text_of(saved);
    for (const std::string& bad :
         {std::string("blokkily-sampler 2\nmode keyed\n"), std::string("garbage"),
          std::string("blokkily-sampler 1\nmode drums\n"),
          std::string("blokkily-sampler 1\npolyphony 0\n"),
          good + "zone \"x.wav\" 60 0 70 50 1 127 0 0 off 0 0 0 0 1 0 0 0 1 0 0\n",
          good + "zone \"x.wav\" 60 0 0 127 1 127 0 0 forward 10 5 0 0 1 0 0 0 1 0 0\n",
          good + "zone \"unterminated 60\n", good + "wobble 3\n"}) {
        require(!parse_sampler(bytes_of(bad)).has_value(), "must refuse: " + bad);
        require(!second.load_state(bytes_of(bad)), "load_state must refuse: " + bad);
        require(second.program() == program, "a refused state changes nothing");
    }
    SamplerZone quoted = low;
    quoted.sample = "odd \"name\" with\\slash\nand newline.wav";
    const auto odd = serialize_sampler(keyed({quoted}));
    const auto odd_back = parse_sampler(odd);
    require(odd_back && odd_back->zones[0].sample == quoted.sample,
            "a path with quotes, backslashes and newlines round-trips");
}

// Sample paths inside the project directory are saved relative to it and
// resolved against it (plan F-F, paths.hpp); others stay absolute.
void relative_paths() {
    SamplerInstrument maker;
    maker.set_base_directory(fixtures);
    auto zone = *maker.make_zone(fixtures / "tones8_48k_pcm16.wav");
    require(zone.sample == "tones8_48k_pcm16.wav",
            "a file in the project directory is named relative to it: " + zone.sample);
    zone.track_pitch = false;
    SamplerZone absolute = zone_for(fixture("sine1k_44k1_pcm16.wav"));
    absolute.low_key = absolute.high_key = 90;

    SamplerInstrument sampler;
    sampler.set_base_directory(fixtures);
    require(sampler.activate(rate, 1, block), "activate");
    require(sampler.set_program(keyed({zone, absolute})) && sampler.missing_samples().empty(),
            "relative and absolute zones load");
    const auto saved = text_of(sampler.save_state());
    require(saved.find("zone \"tones8_48k_pcm16.wav\"") != std::string::npos,
            "a relative zone stays relative");
    require(saved.find("zone \"sine1k_44k1_pcm16.wav\"") != std::string::npos,
            "an absolute path inside the project is saved relative: " + saved);

    // Moved with its project directory, the program still finds its files.
    SamplerInstrument moved;
    moved.set_base_directory(fixtures);
    require(moved.activate(rate, 1, block), "activate");
    require(moved.load_state(bytes_of(saved)) && moved.missing_samples().empty(),
            "the program resolves its files against the project directory");
    const auto audio = render(moved, 2000, {note_on(0, 60)});
    require(probe::rms(audio.window(0, 1440)) > 0.1, "the relative zone plays");

    SamplerInstrument elsewhere;
    elsewhere.set_base_directory(fixtures / "not-here");
    require(elsewhere.load_state(bytes_of(saved)), "a missing file does not fail the state");
    require(elsewhere.missing_samples().size() == 2,
            "against another directory both relative files are missing");

    SamplerInstrument outside;
    outside.set_base_directory(fixtures / "sub");
    require(outside.set_program(keyed({absolute})), "an outside file loads");
    require(text_of(outside.save_state()).find(fixture("sine1k_44k1_pcm16.wav")) !=
                std::string::npos,
            "a file outside the project directory keeps its absolute path");
}

// Parameters: a value sets the base, a modulation an offset on top of it,
// and the two stay distinct, all measured on a constant signal.
void parameters() {
    auto sampler = sampler_with(keyed({dc_zone()}));
    const auto listed = sampler->parameters();
    require(listed.size() == static_cast<std::size_t>(sampler_parameter::count),
            "the sampler lists its seven parameters");
    for (std::size_t i = 0; i < listed.size(); ++i)
        require(listed[i].id == static_cast<std::int32_t>(i) && listed[i].min == 0.0 &&
                    listed[i].max == 1.0 && listed[i].automatable,
                "parameter " + std::to_string(i) + " is described");
    require(listed[0].name == "Gain" && std::abs(gain_at(listed[0].default_value) - 1.0) < 1e-9,
            "gain defaults to unity");

    const auto audio = render(*sampler, 5000,
                              {note_on(0, 60), set_parameter(1000, sampler_parameter::gain, 0.5),
                               modulate(2000, sampler_parameter::gain, 0.1),
                               set_parameter(3000, sampler_parameter::gain, 0.25),
                               modulate(4000, sampler_parameter::gain, 0.0)});
    const auto level = [&](std::size_t from) { return mean(audio.window(from + 50, 900)) / 0.5; };
    const std::pair<std::size_t, double> expected[]{{0, 1.0},
                                                    {1000, gain_at(0.5)},
                                                    {2000, gain_at(0.6)},
                                                    {3000, gain_at(0.35)},
                                                    {4000, gain_at(0.25)}};
    for (const auto& [from, gain] : expected)
        require(std::abs(level(from) / gain - 1.0) < 1e-4,
                "rendered gain at " + std::to_string(from) + " must be " + number(gain) +
                    ", was " + number(level(from)));
    require(audio.left[999] > 0.49F && audio.left[1000] < 0.05F,
            "a parameter change lands on its sample offset");

    // Tune: 0.75 is an octave up.
    auto tuned = sampler_with(keyed({tuned_sine()}));
    const auto octave = render(*tuned, 24000,
                               {set_parameter(0, sampler_parameter::tune, 0.75), note_on(0, 69)});
    const double hz = frequency_near(octave.window(2400, 19200), 880.0, 30.0);
    require(std::abs(hz - 880.0) <= 2.0, "tune 0.75 must play an octave up: " + number(hz));
}

// Pattern locks reach the sampler through the production engine: an
// automation lock sets gain, a modulation lock on a later step adds to it.
void pattern_locks() {
    const auto run = [](std::vector<ParameterLock> first, std::vector<ParameterLock> second) {
        constexpr Tick length = 8192;   // one tick per sample at 120 BPM, 48 kHz
        Pattern pattern(length, 24000);
        Trigger note;
        note.start = 0;
        note.duration = 6000;
        note.musical_data = Note{60, 1.0F, 0.0F};
        note.locks = std::move(first);
        (void)pattern.add(note);
        Trigger later;   // a key outside the zone: it only carries its locks
        later.start = 3000;
        later.duration = 100;
        later.musical_data = Note{100, 1.0F, 0.0F};
        later.locks = std::move(second);
        (void)pattern.add(later);
        Song song;
        song.patterns = {{"Locks", std::move(pattern)}};
        song.tracks = {Track{}};
        song.clips = {{0, 0, 0, 1}};

        SongEngine engine;
        engine.set_instrument(0, sampler_with(keyed({dc_zone()})));
        std::string error;
        require(engine.prepare(song, rate, block, 0, &error), "prepare: " + error);
        engine.set_playing(true);
        std::vector<float> left(6144), right(6144);
        for (std::size_t at = 0; at < left.size(); at += block)
            engine.process({std::span<float>(left.data() + at, block),
                            std::span<float>(right.data() + at, block)});
        return std::pair{mean(std::span<const float>(left).subspan(1000, 1500)),
                         mean(std::span<const float>(left).subspan(3500, 1500))};
    };
    const auto plain = run({}, {});
    require(plain.first > 0.1 && std::abs(plain.second / plain.first - 1.0) < 1e-4,
            "without locks the note holds its level");
    const auto locked =
        run({{"gain", sampler_parameter::gain, 0.5, ParameterLock::Kind::automation}},
            {{"gain", sampler_parameter::gain, 0.1, ParameterLock::Kind::modulation}});
    require(std::abs(locked.first / plain.first / gain_at(0.5) - 1.0) < 1e-4,
            "the automation lock sets the gain: " + number(locked.first / plain.first));
    require(std::abs(locked.second / plain.first / gain_at(0.6) - 1.0) < 1e-4,
            "the modulation lock adds to it: " + number(locked.second / plain.first));
}

// The live kit handoff: a new program reaches the running sampler without
// cutting sounding voices, and old kits are freed once nothing reads them.
void kit_handoff() {
    SamplerInstrument probe_instrument;
    auto looped = *probe_instrument.make_zone(fixture("loop480_48k_pcm16.wav"));
    looped.envelope = {0.0, 0.0, 1.0, 0.005};
    SamplerZone sine = tuned_sine();
    auto sampler = sampler_with(keyed({looped}));
    require(sampler->accepts_state_while_running(), "the sampler takes state while running");

    auto first = render(*sampler, 4800, {note_on(0, 71)});
    require(std::abs(frequency_near(first.window(480, 4096), 480.0, 20.0) - 480.0) <= 2.0,
            "the first kit plays 480 Hz");
    // A new program arrives while note 71 still sounds.
    require(sampler->load_state(serialize_sampler(keyed({sine}))), "the swap loads");
    require(sampler->kit_count() == 2, "old and new kits are both held");
    const auto during = render(*sampler, 4800, {note_on(0, 69)});
    const double old_voice = probe::goertzel_energy(during.window(0, 4800), rate, 480.0);
    const double new_voice = probe::goertzel_energy(during.window(0, 4800), rate, 440.0);
    require(old_voice > 0.01 && new_voice > 0.01,
            "the held note keeps the old kit while the new note plays the new one");
    sampler->collect();
    require(sampler->kit_count() == 2, "a kit a voice still reads is not freed");
    (void)render(*sampler, 4800, {note_off(0, 71)});
    sampler->collect();
    require(sampler->kit_count() == 1, "once its voice has ended the old kit is freed");

    // Two programs before the callback takes either: the skipped one is freed
    // at once and the callback plays the last.
    require(sampler->load_state(serialize_sampler(keyed({looped}))), "swap");
    require(sampler->load_state(serialize_sampler(keyed({tuned_sine()}))), "swap again");
    require(sampler->kit_count() == 2, "a kit the callback never took is freed when replaced");
    (void)render(*sampler, 4800, {note_off(0, 69)});
    const auto last = render(*sampler, 4800, {note_on(0, 69)});
    require(std::abs(frequency_near(last.window(480, 4096), 440.0, 20.0) - 440.0) <= 1.0,
            "the callback plays the last program published");
    sampler->collect();
    require(sampler->kit_count() == 1, "only the live kit remains");
}

const std::map<std::string, std::function<void()>> cases{
    {"file_metadata", file_metadata},
    {"missing_sample", missing_sample},
    {"keyed_pitch", keyed_pitch},
    {"key_velocity_range", key_velocity_range},
    {"velocity", velocity},
    {"envelope", envelope},
    {"loop", loop},
    {"loop_seam", loop_seam},
    {"kit_slices", kit_slices},
    {"one_shot_and_choke", one_shot_and_choke},
    {"state", state},
    {"relative_paths", relative_paths},
    {"parameters", parameters},
    {"pattern_locks", pattern_locks},
    {"kit_handoff", kit_handoff},
};

} // namespace

int main(int argc, char** argv) {
    try {
        require(argc == 2, "pass a case name");
        const auto found = cases.find(argv[1]);
        require(found != cases.end(), std::string("unknown case ") + argv[1]);
        found->second();
        std::cout << argv[1] << " passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << (argc > 1 ? argv[1] : "") << ": " << error.what() << '\n';
        return 1;
    }
}
