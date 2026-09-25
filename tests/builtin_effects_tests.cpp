// Built-in effect DSP and the effect fixtures (plan item 1.8), proved from
// rendered audio. The built-ins are driven only through the PluginInstance
// interface they will be hosted by: activate, parameter events at sample
// offsets, set_transport, process in place, and the state stream. The effect
// fixtures are a real .clap loaded through its official clap_entry by
// ClapPluginInstance and a real .vst3 bundle hosted by Vst3PluginInstance.
//
// Run with a case name; each case is its own CTest test (builtin_<case>).
// features/builtin_effects.feature maps its scenarios to these cases.

#include "support/audio_probe.hpp"

#include "blokkily/effects/builtin.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

constexpr double rate = 48000.0;
constexpr std::size_t max_block = 512;

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

std::string number(double value) { return std::to_string(value); }

struct Stereo {
    std::vector<float> left, right;
    explicit Stereo(std::size_t frames = 0) : left(frames, 0.0F), right(frames, 0.0F) {}
    [[nodiscard]] std::size_t size() const { return left.size(); }
};

// An event at an absolute sample of the render.
struct Timed {
    std::size_t at;
    PluginEvent event;
};

PluginEvent value_event(std::int32_t id, double value) {
    return {PluginEvent::Type::parameter_value, 0, id, value};
}
PluginEvent modulation_event(std::int32_t id, double amount) {
    return {PluginEvent::Type::parameter_modulation, 0, id, amount};
}

// Plays `input` through `effect` in blocks of `block` frames, handing each
// event to the block that holds its sample, at its offset inside that block,
// and the transport (when given) before every block.
Stereo render(PluginInstance& effect, const Stereo& input, std::size_t block,
              const std::vector<Timed>& events = {}, const TransportInfo* transport = nullptr) {
    Stereo output = input;
    std::vector<PluginEvent> due;
    for (std::size_t start = 0; start < input.size(); start += block) {
        const std::size_t frames = std::min(block, input.size() - start);
        due.clear();
        for (const auto& timed : events)
            if (timed.at >= start && timed.at < start + frames) {
                auto event = timed.event;
                event.sample_offset = static_cast<std::uint32_t>(timed.at - start);
                due.push_back(event);
            }
        if (transport) effect.set_transport(*transport);
        effect.process({std::span(output.left).subspan(start, frames),
                        std::span(output.right).subspan(start, frames)},
                       due);
    }
    return output;
}

std::unique_ptr<PluginInstance> builtin(const char* identifier) {
    auto effect = create_builtin_effect(identifier);
    require(effect != nullptr, std::string("built-in effect ") + identifier + " must exist");
    require(effect->activate(rate, 1, max_block),
            std::string("built-in effect ") + identifier + " must activate");
    return effect;
}

Stereo sine(double frequency, double amplitude, std::size_t frames) {
    Stereo signal(frames);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const auto sample = static_cast<float>(
            amplitude * std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(frame) / rate));
        signal.left[frame] = sample;
        signal.right[frame] = sample;
    }
    return signal;
}

Stereo impulse(std::size_t frames, std::size_t at = 0) {
    Stereo signal(frames);
    signal.left[at] = 1.0F;
    signal.right[at] = 1.0F;
    return signal;
}

Stereo constant(std::size_t frames, float value) {
    Stereo signal(frames);
    std::fill(signal.left.begin(), signal.left.end(), value);
    std::fill(signal.right.begin(), signal.right.end(), value);
    return signal;
}

std::span<const float> window(const std::vector<float>& samples, std::size_t from, std::size_t to) {
    return std::span<const float>(samples).subspan(from, to - from);
}

double decibels(double ratio) { return 20.0 * std::log10(ratio); }

bool same_bits(const Stereo& a, const Stereo& b) {
    return a.size() == b.size() &&
           std::memcmp(a.left.data(), b.left.data(), a.size() * sizeof(float)) == 0 &&
           std::memcmp(a.right.data(), b.right.data(), a.size() * sizeof(float)) == 0;
}

// ------------------------------------------------------------------- EQ ----

// The gain, in dB, the EQ set by `settings` gives a sine at `frequency`:
// Goertzel energies of the last second (a whole number of cycles) out/in.
double eq_gain_db(const std::vector<Timed>& settings, double frequency) {
    auto eq = builtin("eq3");
    const auto input = sine(frequency, 0.25, 72000);
    const auto output = render(*eq, input, 256, settings);
    const double in = probe::goertzel_energy(window(input.left, 24000, 72000), rate, frequency);
    const double out_left = probe::goertzel_energy(window(output.left, 24000, 72000), rate, frequency);
    const double out_right = probe::goertzel_energy(window(output.right, 24000, 72000), rate, frequency);
    require(std::abs(out_left - out_right) <= 1e-9 * std::max(out_left, 1e-12) + 1e-15,
            "both channels are equalised alike");
    return 10.0 * std::log10(out_left / in);
}

void expect_gain(const std::vector<Timed>& settings, double frequency, double expected_db,
                 const char* what) {
    const double measured = eq_gain_db(settings, frequency);
    std::cout << what << ": " << frequency << " Hz measured " << measured << " dB, expected "
              << expected_db << " dB\n";
    require(std::abs(measured - expected_db) <= 1.0,
            std::string(what) + ": " + number(frequency) + " Hz must be " + number(expected_db) +
                " dB +-1, measured " + number(measured));
}

void eq3_response() {
    // Flat by default: a sine passes at its own level.
    expect_gain({}, 1000.0, 0.0, "flat");
    const double flat = eq_gain_db({}, 1000.0);
    require(std::abs(flat) < 0.01, "a flat EQ changes nothing, measured " + number(flat));

    const std::vector<Timed> low{{0, value_event(eq3::low_gain_db, 6.0)},
                                 {0, value_event(eq3::low_hz, 200.0)}};
    expect_gain(low, 40.0, 6.0, "low shelf +6 dB at 200 Hz");
    expect_gain(low, 8000.0, 0.0, "low shelf leaves the highs");

    const std::vector<Timed> mid{{0, value_event(eq3::mid_gain_db, -6.0)},
                                 {0, value_event(eq3::mid_hz, 1000.0)},
                                 {0, value_event(eq3::mid_q, 1.0)}};
    expect_gain(mid, 1000.0, -6.0, "mid -6 dB at 1 kHz");
    expect_gain(mid, 50.0, 0.0, "mid band leaves the lows");
    expect_gain(mid, 15000.0, 0.0, "mid band leaves the highs");

    const std::vector<Timed> high{{0, value_event(eq3::high_gain_db, 9.0)},
                                  {0, value_event(eq3::high_hz, 4000.0)}};
    expect_gain(high, 16000.0, 9.0, "high shelf +9 dB at 4 kHz");
    expect_gain(high, 100.0, 0.0, "high shelf leaves the lows");

    const std::vector<Timed> cut{{0, value_event(eq3::low_gain_db, -12.0)}};
    expect_gain(cut, 30.0, -12.0, "low shelf -12 dB");
}

// ---------------------------------------------------------------- Delay ----

void delay_echoes() {
    // Defaults: 250 ms, feedback 0.5, mix 0.5.
    auto delay = builtin("delay");
    const auto output = render(*delay, impulse(48000), 256);
    const std::size_t echo = 12000; // 250 ms at 48 kHz
    for (const auto* channel : {&output.left, &output.right}) {
        const auto& samples = *channel;
        require(std::abs(samples[0] - 0.5F) < 1e-6F, "the dry impulse passes at 1 - mix");
        const auto first = probe::first_nonzero(window(samples, 1, samples.size()));
        require(first && *first + 1 == echo,
                "the first echo arrives at 250 ms (sample 12000), found at " +
                    (first ? number(static_cast<double>(*first + 1)) : std::string("none")));
        require(std::abs(samples[echo] - 0.5F) < 1e-6F,
                "the first echo is at the mix level, got " + number(samples[echo]));
        require(std::abs(samples[2 * echo] - 0.25F) < 1e-6F,
                "the second echo arrives at 500 ms, at half the first, got " +
                    number(samples[2 * echo]));
        require(std::abs(samples[2 * echo] / samples[echo] - 0.5F) < 1e-6F,
                "successive echoes keep the feedback ratio 0.5");
        require(std::abs(samples[3 * echo] - 0.125F) < 1e-6F, "the third echo keeps halving");
        // Nothing sounds between the echoes.
        double between = 0.0;
        for (std::size_t frame = 1; frame < samples.size(); ++frame)
            if (frame % echo != 0) between = std::max(between, static_cast<double>(std::abs(samples[frame])));
        require(between == 0.0, "silence between the echoes, found " + number(between));
    }
    // It keeps sounding until its echoes are 80 dB down, and says so.
    const auto tail = delay->tail_samples();
    require(tail == echo * 14, "the delay reports a tail of 14 echoes, got " + number(static_cast<double>(tail)));
    auto again = builtin("delay");
    const auto long_output = render(*again, impulse(tail + 24000), 512);
    require(probe::peak(window(long_output.left, tail - echo + 1, tail + 1)) > 0.0F,
            "the last echo inside the reported tail still sounds");
    require(probe::peak(window(long_output.left, tail + 1, long_output.size())) < 1e-4F,
            "past the reported tail the echoes are 80 dB down");
}

void delay_tempo_sync() {
    // Synced to half a beat: 250 ms at 120 BPM, 333 ms at 90 BPM, from the
    // tempo set_transport() reports.
    const std::vector<Timed> synced{{0, value_event(delay::sync, 1.0)},
                                    {0, value_event(delay::beats, 0.5)},
                                    {0, value_event(delay::time_ms, 1000.0)}};
    for (const auto& [bpm, expected] : {std::pair{120.0, std::size_t{12000}},
                                        std::pair{90.0, std::size_t{16000}}}) {
        auto delay = builtin("delay");
        const TransportInfo transport{bpm, 0.0, 0, 4, 4, true};
        const auto output = render(*delay, impulse(40000), 256, synced, &transport);
        const auto first = probe::first_nonzero(window(output.left, 1, output.size()));
        require(first && *first + 1 == expected,
                "a synced half-beat echo at " + number(bpm) + " BPM arrives at sample " +
                    number(static_cast<double>(expected)) + ", found " +
                    (first ? number(static_cast<double>(*first + 1)) : std::string("none")));
    }
    // Unsynced, the tempo is ignored and the time in milliseconds holds.
    auto free = builtin("delay");
    const TransportInfo fast{200.0, 0.0, 0, 4, 4, true};
    const auto output = render(*free, impulse(20000), 256, {}, &fast);
    const auto first = probe::first_nonzero(window(output.left, 1, output.size()));
    require(first && *first + 1 == 12000, "an unsynced delay ignores the tempo");
}

// --------------------------------------------------------------- Reverb ----

void reverb_tail() {
    auto reverb = builtin("reverb");
    const auto tail = reverb->tail_samples();
    std::cout << "reverb tail " << tail << " samples (" << static_cast<double>(tail) / rate << " s)\n";
    require(tail > 48000 && tail < 20 * 48000, "the reverb reports a tail of 1 to 20 seconds");
    const auto output = render(*reverb, impulse(tail + 24000), 512);
    require(std::abs(output.left[0] - 0.7F) < 1e-6F, "the dry impulse passes at 1 - mix");
    const double early = probe::rms(window(output.left, 2400, 16800));   // 50-350 ms
    const double late = probe::rms(window(output.left, 48000, 62400));   // 1.0-1.3 s
    const double later = probe::rms(window(output.left, 96000, 110400)); // 2.0-2.3 s
    std::cout << "reverb rms 50-350 ms " << early << ", 1.0-1.3 s " << late << ", 2.0-2.3 s "
              << later << '\n';
    require(early > 1e-3, "the impulse leaves a tail after the dry sound, rms " + number(early));
    require(probe::rms(window(output.right, 2400, 16800)) > 1e-3, "the right channel has a tail too");
    require(late < early * 0.5 && later < late * 0.5, "the tail decays");
    require(late > 0.0 && later > 0.0, "the tail is still sounding after a second");
    bool differ = false;
    for (std::size_t frame = 2400; frame < 16800 && !differ; ++frame)
        differ = output.left[frame] != output.right[frame];
    require(differ, "at full width the two channels' tails differ");
    const float end = std::max(probe::peak(window(output.left, tail - 4800, tail)),
                               probe::peak(window(output.right, tail - 4800, tail)));
    require(end < 1e-4F, "by the reported tail the reverb is 80 dB down, peak " + number(end));
}

// ----------------------------------------------------------- Compressor ----

double settled_peak_db(const std::vector<Timed>& settings, double amplitude) {
    auto compressor = builtin("compressor");
    const auto output = render(*compressor, sine(1000.0, amplitude, 48000), 256, settings);
    return decibels(std::max(probe::peak(window(output.left, 36000, 48000)),
                             probe::peak(window(output.right, 36000, 48000))));
}

void compressor_level() {
    // Threshold -20 dBFS, ratio 4:1: a -6 dBFS tone is 14 dB over, which
    // comes out 3.5 dB over, at -16.5 dBFS.
    const double loud = settled_peak_db({}, 0.5);
    std::cout << "compressor: -6.02 dBFS in, " << loud << " dBFS out\n";
    require(std::abs(loud - (-20.0 + (decibels(0.5) + 20.0) / 4.0)) < 0.5,
            "a -6 dBFS tone settles near -16.5 dBFS, got " + number(loud));
    // Below the threshold nothing changes.
    const double quiet = settled_peak_db({}, 0.05);
    require(std::abs(quiet - decibels(0.05)) < 0.05,
            "a tone under the threshold passes unchanged, got " + number(quiet));
    // Makeup gain lifts the compressed level.
    const double made_up = settled_peak_db({{0, value_event(compressor::makeup_db, 6.0)}}, 0.5);
    require(std::abs(made_up - (loud + 6.0)) < 0.1, "6 dB of makeup adds 6 dB, got " + number(made_up));
    // A harder ratio compresses harder: 10:1 leaves 1.4 dB of the 14 over.
    const double hard = settled_peak_db({{0, value_event(compressor::ratio, 10.0)}}, 0.5);
    require(std::abs(hard - (-20.0 + (decibels(0.5) + 20.0) / 10.0)) < 0.5,
            "10:1 settles near -18.6 dBFS, got " + number(hard));
}

// ---------------------------------------------------------- Determinism ----

Stereo test_signal(std::size_t frames) {
    Stereo signal(frames);
    std::uint32_t seed = 12345;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        seed = seed * 1664525U + 1013904223U;
        const double noise = static_cast<double>(seed >> 8) / 16777216.0 - 0.5;
        const double tone = 0.4 * std::sin(2.0 * std::numbers::pi * 220.0 * static_cast<double>(frame) / rate);
        signal.left[frame] = static_cast<float>(tone + 0.3 * noise);
        signal.right[frame] = static_cast<float>(tone - 0.2 * noise);
    }
    return signal;
}

void determinism() {
    const auto input = test_signal(96000);
    const TransportInfo transport{120.0, 0.0, 0, 4, 4, true};
    const std::map<std::string, std::vector<Timed>> moves{
        {"eq3", {{30011, value_event(eq3::mid_gain_db, 9.0)}, {50003, modulation_event(eq3::low_gain_db, -6.0)}}},
        {"delay", {{30011, value_event(delay::time_ms, 100.0)}, {50003, modulation_event(delay::feedback, 0.3)}}},
        {"reverb", {{30011, value_event(reverb::size, 0.9)}, {50003, modulation_event(reverb::width, -0.5)}}},
        {"compressor", {{30011, value_event(compressor::threshold_db, -30.0)}, {50003, modulation_event(compressor::ratio, 4.0)}}},
    };
    for (const auto& info : builtin_effects()) {
        const auto& events = moves.at(info.identifier);
        auto first = builtin(info.identifier.c_str());
        auto second = builtin(info.identifier.c_str());
        auto odd = builtin(info.identifier.c_str());
        const auto a = render(*first, input, 256, events, &transport);
        const auto b = render(*second, input, 256, events, &transport);
        const auto c = render(*odd, input, 97, events, &transport);
        require(!same_bits(a, input), info.identifier + " must change the signal");
        require(same_bits(a, b), info.identifier + " renders bit-identically twice");
        require(same_bits(a, c), info.identifier + " renders bit-identically in any block size");
        // Activating again starts from silence, as a fresh instance does.
        require(first->activate(rate, 1, max_block), "reactivation");
        const auto again = render(*first, input, 256, {}, &transport);
        auto fresh = builtin(info.identifier.c_str());
        // The re-activated instance keeps the automated base values; a fresh
        // one given the same values in its first block must match it.
        std::vector<Timed> base_values;
        for (const auto& timed : events)
            if (timed.event.type == PluginEvent::Type::parameter_value)
                base_values.push_back({0, timed.event});
        const auto expected = render(*fresh, input, 256, base_values, &transport);
        require(same_bits(again, expected),
                info.identifier + " forgets its signal state and its modulation on activate");
    }
}

// ------------------------------------------------ Interface and state -----

void builtin_interface() {
    const auto list = builtin_effects();
    std::string names;
    for (const auto& info : list) names += info.identifier + ";";
    require(names == "eq3;delay;reverb;compressor;", "the built-ins are listed in order, got " + names);
    require(create_builtin_effect("flanger") == nullptr, "an unknown built-in is refused");
    const std::map<std::string, std::string> parameters{
        {"eq3", "Low Gain;Low Freq;Mid Gain;Mid Freq;Mid Q;High Gain;High Freq;"},
        {"delay", "Time;Feedback;Mix;Sync;Beats;"},
        {"reverb", "Size;Damping;Width;Mix;"},
        {"compressor", "Threshold;Ratio;Attack;Release;Makeup;"},
    };
    for (const auto& info : list) {
        auto effect = create_builtin_effect(info.identifier);
        require(effect->format() == builtin_effect_format, "a built-in's format is Built-in");
        require(effect->ports().audio_inputs == 2 && !effect->ports().note_input,
                info.identifier + " takes stereo audio and no notes");
        require(effect->latency_samples() == 0, info.identifier + " adds no latency");
        std::string listed;
        std::int32_t expected_id = 0;
        for (const auto& parameter : effect->parameters()) {
            require(parameter.id == expected_id++ && parameter.automatable &&
                        parameter.min <= parameter.default_value &&
                        parameter.default_value <= parameter.max,
                    info.identifier + " parameter " + parameter.name + " is well formed");
            listed += parameter.name + ";";
        }
        require(listed == parameters.at(info.identifier),
                info.identifier + " lists its parameters, got " + listed);
        // Not yet activated, a built-in leaves the signal alone.
        auto signal = constant(64, 0.5F);
        effect->process({signal.left, signal.right}, {});
        require(signal.left[63] == 0.5F, info.identifier + " passes audio before activation");
    }

    // Values land at their sample offset, modulation adds to the base and is
    // clamped to the range, and notes are ignored.
    auto delay = builtin("delay");
    const std::vector<Timed> moves{{0, value_event(delay::mix, 0.0)},
                                   {100, value_event(delay::mix, 1.0)},
                                   {200, {PluginEvent::Type::note_on, 0, 60, 1.0}},
                                   {300, value_event(delay::mix, 0.0)},
                                   {400, modulation_event(delay::mix, 5.0)},
                                   {500, modulation_event(delay::mix, 0.0)}};
    const auto output = render(*delay, constant(600, 1.0F), 256, moves);
    for (std::size_t frame = 0; frame < 600; ++frame) {
        const bool wet = (frame >= 100 && frame < 300) || (frame >= 400 && frame < 500);
        require(output.left[frame] == (wet ? 0.0F : 1.0F),
                "sample " + number(static_cast<double>(frame)) + " must be " +
                    (wet ? "all echo (still silent)" : "all dry"));
    }

    // State: what one instance saves another loads, and renders the same.
    auto configured = builtin("reverb");
    const auto input = test_signal(24000);
    (void)render(*configured, constant(8, 0.0F), 8,
                 {{0, value_event(reverb::size, 0.2)}, {0, value_event(reverb::damping, 0.9)},
                  {0, value_event(reverb::mix, 0.8)}});
    const auto state = configured->save_state();
    auto restored = create_builtin_effect("reverb");
    require(restored->load_state(state), "a built-in loads its own state");
    require(restored->activate(rate, 1, max_block), "and activates after");
    require(configured->activate(rate, 1, max_block), "re-activation clears the signal");
    require(same_bits(render(*configured, input, 256), render(*restored, input, 256)),
            "a restored built-in renders exactly as the one that saved it");
    auto defaults = builtin("reverb");
    require(!same_bits(render(*defaults, input, 256), render(*restored, input, 256)),
            "and the state really changed the sound");
    // A stream that is not a built-in's state is refused and changes nothing.
    std::vector<std::byte> garbage(state.size(), std::byte{0x5A});
    require(!restored->load_state(garbage), "garbage state is refused");
    require(!restored->load_state(std::span(state).first(state.size() - 1)),
            "truncated state is refused");
    require(restored->save_state() == state, "a refused load leaves the parameters alone");
}

// --------------------------------------------------- Effect fixtures ------

constexpr const char* clap_effect_id = "dev.blokkily.test.effect";

std::unique_ptr<ClapPluginInstance> clap_effect() {
    std::string error;
    auto plugin = ClapPluginInstance::create(BLOKKILY_TEST_CLAP_EFFECT_PATH, clap_effect_id, &error);
    require(plugin != nullptr, "the CLAP effect must load through its clap_entry: " + error);
    return plugin;
}

std::unique_ptr<Vst3PluginInstance> vst3_effect() {
    std::string error;
    auto plugin = Vst3PluginInstance::create(BLOKKILY_TEST_VST3_EFFECT_PATH, 0, &error);
    require(plugin != nullptr, "the VST3 effect bundle must load through JUCE: " + error);
    return plugin;
}

bool near(float actual, float expected) { return std::abs(actual - expected) < 1e-6F; }

// Every sample in [from, to) of both channels equals `expected`.
void require_level(const Stereo& audio, std::size_t from, std::size_t to, float expected,
                   const std::string& what) {
    for (std::size_t frame = from; frame < to; ++frame)
        require(near(audio.left[frame], expected) && near(audio.right[frame], expected),
                what + ": sample " + number(static_cast<double>(frame)) + " must be " +
                    number(expected) + ", got " + number(audio.left[frame]));
}

void clap_effect_fixture() {
    auto effect = clap_effect();
    require(effect->ports().audio_inputs == 2 && !effect->ports().note_input,
            "the CLAP effect takes stereo audio and no notes");
    const auto parameters = effect->parameters();
    require(parameters.size() == 1 && parameters[0].id == 0 && parameters[0].name == "Gain" &&
                parameters[0].default_value == 0.25 && parameters[0].automatable,
            "the CLAP effect lists Gain (id 0, default 0.25)");
    require(effect->activate(rate, 1, 256), "the CLAP effect activates");
    require(effect->latency_samples() == 64 && effect->tail_samples() == 64,
            "the CLAP effect reports 64 samples of latency and tail");

    // A steady input comes out 64 samples late at the gain; a gain change and
    // a modulation land at their sample offsets.
    const std::vector<Timed> moves{{356, value_event(0, 0.5)}, {600, modulation_event(0, 0.25)}};
    const auto output = render(*effect, constant(768, 1.0F), 256, moves);
    require_level(output, 0, 64, 0.0F, "latency");
    require_level(output, 64, 356, 0.25F, "default gain");
    require_level(output, 356, 600, 0.5F, "gain set at sample 356");
    require_level(output, 600, 768, 0.75F, "modulated at sample 600");

    // An impulse in comes out once, 64 samples later: the input really is the
    // block the host handed over.
    auto clean = clap_effect();
    require(clean->activate(rate, 1, 256), "a second instance activates");
    auto echo = render(*clean, impulse(256, 5), 256);
    require(near(echo.left[69], 0.25F) && near(echo.right[69], 0.25F), "the impulse arrives at 69");
    echo.left[69] = echo.right[69] = 0.0F;
    require(probe::peak(echo.left) == 0.0F && probe::peak(echo.right) == 0.0F, "and nowhere else");

    // State: the gain travels in the stream and is heard after loading.
    const auto state = effect->save_state();
    auto restored = clap_effect();
    require(restored->load_state(state), "the CLAP effect loads its saved state");
    require(restored->activate(rate, 1, 256), "and activates");
    const auto after = render(*restored, constant(256, 1.0F), 256);
    require_level(after, 64, 256, 0.5F, "restored gain");
    const std::vector<std::byte> garbage{std::byte{1}, std::byte{2}, std::byte{3}};
    require(!restored->load_state(garbage), "garbage state is refused by the fixture");
}

void vst3_effect_fixture() {
    auto effect = vst3_effect();
    require(effect->ports().audio_inputs == 2 && !effect->ports().note_input,
            "the VST3 effect takes stereo audio and no notes");
    const auto parameters = effect->parameters();
    require(!parameters.empty() && parameters[0].id == 0 && parameters[0].name == "Gain" &&
                std::abs(parameters[0].default_value - 0.25) < 1e-6,
            "the VST3 effect lists Gain first, default 0.25");
    require(effect->activate(rate, 1, 256), "the VST3 effect activates");
    require(effect->latency_samples() == 32 && effect->tail_samples() == 32,
            "the VST3 effect reports 32 samples of latency and tail, got " +
                number(effect->latency_samples()) + " and " +
                number(static_cast<double>(effect->tail_samples())));

    const std::vector<Timed> moves{{300, value_event(0, 0.5)}};
    const auto output = render(*effect, constant(512, 1.0F), 256, moves);
    require_level(output, 0, 32, 0.0F, "latency");
    require_level(output, 32, 300, -0.25F, "default gain, inverted");
    require_level(output, 300, 512, -0.5F, "gain set at sample 300");

    const auto state = effect->save_state();
    auto restored = vst3_effect();
    require(restored->load_state(state), "the VST3 effect loads its saved state");
    require(restored->activate(rate, 1, 256), "and activates");
    const auto after = render(*restored, constant(256, 1.0F), 256);
    require_level(after, 32, 256, -0.5F, "restored gain");
}

// Both fixtures in one chain, in place on one block: the CLAP effect's 0.25
// into the VST3 effect's -0.25 renders -0.0625, 64 + 32 samples late.
void fixture_chain() {
    auto clap = clap_effect();
    auto vst3 = vst3_effect();
    require(clap->activate(rate, 1, 256) && vst3->activate(rate, 1, 256), "both activate");
    const std::uint32_t latency = clap->latency_samples() + vst3->latency_samples();
    require(latency == 96, "the chain's latency is the sum, 96");
    auto audio = constant(512, 1.0F);
    for (std::size_t start = 0; start < audio.size(); start += 256) {
        const StereoBlock block{std::span(audio.left).subspan(start, 256),
                                std::span(audio.right).subspan(start, 256)};
        clap->process(block, {});
        vst3->process(block, {});
    }
    require_level(audio, 0, latency, 0.0F, "the chain's latency");
    require_level(audio, latency, 512, -0.0625F, "the chain");
}

const std::map<std::string, std::function<void()>> cases{
    {"eq3_response", eq3_response},
    {"delay_echoes", delay_echoes},
    {"delay_tempo_sync", delay_tempo_sync},
    {"reverb_tail", reverb_tail},
    {"compressor_level", compressor_level},
    {"determinism", determinism},
    {"interface", builtin_interface},
    {"clap_effect", clap_effect_fixture},
    {"vst3_effect", vst3_effect_fixture},
    {"fixture_chain", fixture_chain},
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
