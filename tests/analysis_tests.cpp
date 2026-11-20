// Render analysis: loudness, true peak and where the energy sits, measured
// from signals whose answers are known. The loudness cases are the EBU's
// own (Tech 3341 for integrated loudness, Tech 3342 for loudness range).
//
// Run with a case name; each case is its own CTest test (analysis_<case>).
// features/render_analysis.feature names the scenario each case executes.

#include "blokkily/audio/analysis.hpp"

#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {

void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

void near(double actual, double expected, double tolerance, const std::string& what) {
    require(std::abs(actual - expected) <= tolerance,
            what + ": expected " + std::to_string(expected) + " +/- " + std::to_string(tolerance) +
                ", measured " + std::to_string(actual));
}

double amplitude(double dbfs) { return std::pow(10.0, dbfs / 20.0); }

// Appends `seconds` of a stereo sine (both channels alike) to `out`.
void sine(std::vector<float>& out, double rate, double hz, double dbfs, double seconds,
          double phase = 0.0, double right_sign = 1.0) {
    const auto frames = static_cast<std::size_t>(std::lround(seconds * rate));
    const double peak = amplitude(dbfs);
    const std::size_t start = out.size() / 2;
    for (std::size_t frame = 0; frame < frames; ++frame) {
        const double t = static_cast<double>(start + frame) / rate;
        const auto sample = static_cast<float>(peak * std::sin(2.0 * std::numbers::pi * hz * t + phase));
        out.push_back(sample);
        out.push_back(static_cast<float>(right_sign) * sample);
    }
}

const BandEnergy& band(const AudioAnalysis& analysis, const std::string& name) {
    for (const auto& entry : analysis.bands)
        if (entry.name == name) return entry;
    throw std::runtime_error("no band " + name);
}

// Tech 3341 cases 1 and 2: a stereo 1 kHz sine at -23 and -33 dBFS reads
// -23 and -33 LUFS, at 48 kHz and at 44.1 kHz.
void sine_reads_its_loudness() {
    for (const double rate : {48000.0, 44100.0})
        for (const double level : {-23.0, -33.0}) {
            std::vector<float> audio;
            sine(audio, rate, 1000.0, level, 20.0);
            const auto result = analyze_audio(audio, 2, rate);
            const std::string at = " at " + std::to_string(level) + " dBFS, " +
                                   std::to_string(rate) + " Hz";
            near(result.integrated_lufs, level, 0.1, "integrated" + at);
            near(result.momentary_max_lufs, level, 0.1, "momentary max" + at);
            near(result.short_term_max_lufs, level, 0.1, "short-term max" + at);
            near(result.loudness_range_lu, 0.0, 0.1, "loudness range" + at);
            near(result.sample_peak_dbfs, level, 0.01, "sample peak" + at);
            near(result.rms_dbfs, level - 3.0103, 0.01, "rms" + at);
            near(result.crest_db, 3.0103, 0.02, "crest" + at);
            near(result.seconds, 20.0, 1e-9, "seconds" + at);
        }
}

// Tech 3341 cases 3 and 4: quiet passages around a -23 dBFS stretch are gated
// out, absolutely below -70 LUFS and relatively 10 LU under the rest.
void quiet_passages_are_gated() {
    const double rate = 48000.0;
    {
        std::vector<float> audio;
        sine(audio, rate, 1000.0, -36.0, 10.0);
        sine(audio, rate, 1000.0, -23.0, 60.0);
        sine(audio, rate, 1000.0, -36.0, 10.0);
        near(analyze_audio(audio, 2, rate).integrated_lufs, -23.0, 0.1, "case 3");
    }
    {
        std::vector<float> audio;
        sine(audio, rate, 1000.0, -72.0, 10.0);
        sine(audio, rate, 1000.0, -36.0, 10.0);
        sine(audio, rate, 1000.0, -23.0, 60.0);
        sine(audio, rate, 1000.0, -36.0, 10.0);
        sine(audio, rate, 1000.0, -72.0, 10.0);
        const auto result = analyze_audio(audio, 2, rate);
        near(result.integrated_lufs, -23.0, 0.1, "case 4");
        // The loudest 3 s lies in the -23 dBFS stretch, which starts at 20 s.
        require(result.short_term_max_at >= 19.9 && result.short_term_max_at <= 77.0,
                "the loudest 3 s is placed in the loud stretch, at " +
                    std::to_string(result.short_term_max_at));
    }
}

// Tech 3342 cases 1 and 2: 20 s at -20 dBFS then 20 s at -30 (or -15) has a
// loudness range of 10 (or 5) LU.
void loudness_range_spans_the_passages() {
    const double rate = 48000.0;
    const std::vector<std::pair<double, double>> cases{{-30.0, 10.0}, {-15.0, 5.0}};
    for (const auto& [second, expected] : cases) {
        std::vector<float> audio;
        sine(audio, rate, 1000.0, -20.0, 20.0);
        sine(audio, rate, 1000.0, second, 20.0);
        near(analyze_audio(audio, 2, rate).loudness_range_lu, expected, 1.0,
             "loudness range of -20 then " + std::to_string(second));
    }
}

// A sine at a quarter of the rate, 45 degrees off its samples, has every
// sample at 0.707 of its crest: the samples read -3 dBFS, the true peak 0.
void true_peak_finds_the_crest_between_samples() {
    const double rate = 48000.0;
    std::vector<float> audio;
    sine(audio, rate, rate / 4.0, 0.0, 1.0, std::numbers::pi / 4.0);
    const auto result = analyze_audio(audio, 2, rate);
    near(result.sample_peak_dbfs, -3.0103, 0.01, "sample peak");
    near(result.true_peak_dbtp, 0.0, 0.2, "true peak");
    // Below full scale nothing clips; a sample at full scale does.
    require(result.clipped_samples == 0, "nothing clipped");
    std::vector<float> slow;
    sine(slow, rate, 100.0, 0.0, 1.0);
    const auto full = analyze_audio(slow, 2, rate);
    near(full.true_peak_dbtp, full.sample_peak_dbfs, 0.05, "a slow sine's true peak is its sample peak");
    slow[200] = 1.25F;
    require(analyze_audio(slow, 2, rate).clipped_samples >= 1, "a sample past full scale is counted");
}

// A sine's energy is in the band holding its frequency, at its level; two
// equal sines split the energy evenly and pull the centroid between them.
void energy_sits_in_its_band() {
    const double rate = 48000.0;
    const std::vector<std::pair<double, std::string>> tones{
        {40.0, "sub"},       {120.0, "bass"},       {350.0, "lowMid"}, {1000.0, "mid"},
        {3000.0, "highMid"}, {5000.0, "presence"}, {10000.0, "air"}};
    for (const auto& [hz, name] : tones) {
        std::vector<float> audio;
        sine(audio, rate, hz, -6.0, 4.0);
        const auto result = analyze_audio(audio, 2, rate);
        const auto& holding = band(result, name);
        require(holding.share > 0.97, std::to_string(hz) + " Hz is in " + name + ", share " +
                                          std::to_string(holding.share));
        near(holding.level_db, -6.0 - 3.0103, 0.2, name + " level of a -6 dBFS sine");
        near(result.spectral_centroid_hz, hz, std::max(5.0, hz * 0.02), "centroid of " + name);
    }
    std::vector<float> pair(static_cast<std::size_t>(rate * 4.0) * 2, 0.0F);
    for (std::size_t frame = 0; frame < pair.size() / 2; ++frame) {
        const double t = static_cast<double>(frame) / rate;
        const auto sample = static_cast<float>(0.25 * std::sin(2.0 * std::numbers::pi * 100.0 * t) +
                                               0.25 * std::sin(2.0 * std::numbers::pi * 3000.0 * t));
        pair[frame * 2] = pair[frame * 2 + 1] = sample;
    }
    const auto both = analyze_audio(pair, 2, rate);
    near(band(both, "bass").share, 0.5, 0.03, "bass half");
    near(band(both, "highMid").share, 0.5, 0.03, "high-mid half");
    near(both.spectral_centroid_hz, 1550.0, 40.0, "centroid between the two");
}

// Correlation reads +1 for a mono mix in both speakers and -1 for one channel
// inverted; silence has no loudness at all.
void stereo_image_and_silence() {
    const double rate = 48000.0;
    std::vector<float> same;
    sine(same, rate, 440.0, -12.0, 2.0);
    near(analyze_audio(same, 2, rate).correlation, 1.0, 1e-6, "mono in both speakers");
    std::vector<float> inverted;
    sine(inverted, rate, 440.0, -12.0, 2.0, 0.0, -1.0);
    near(analyze_audio(inverted, 2, rate).correlation, -1.0, 1e-6, "one side inverted");
    const std::vector<float> silence(static_cast<std::size_t>(rate) * 4, 0.0F);
    const auto quiet = analyze_audio(silence, 2, rate);
    require(std::isinf(quiet.integrated_lufs) && quiet.integrated_lufs < 0, "silence has no loudness");
    require(std::isinf(quiet.true_peak_dbtp) && std::isinf(quiet.rms_dbfs), "silence has no level");
    require(std::isnan(quiet.correlation) && std::isnan(quiet.spectral_centroid_hz),
            "silence has no image or centroid");
    const auto nothing = analyze_audio({}, 2, rate);
    require(nothing.seconds == 0.0 && std::isinf(nothing.integrated_lufs), "an empty render");
}

// Each channel reads its own level, and a channel with nothing on it beside
// one that sounds is named: a side lost on the way out.
void one_side_silent_is_named() {
    const double rate = 48000.0;
    std::vector<float> left_only;
    sine(left_only, rate, 440.0, -12.0, 2.0, 0.0, 0.0);
    const auto lost = analyze_audio(left_only, 2, rate);
    require(lost.channel_levels.size() == 2, "a level for each channel");
    near(lost.channel_levels[0].sample_peak_dbfs, -12.0, 0.01, "the left side's peak");
    near(lost.channel_levels[0].rms_dbfs, -15.01, 0.01, "the left side's RMS");
    require(std::isinf(lost.channel_levels[1].sample_peak_dbfs) &&
                std::isinf(lost.channel_levels[1].rms_dbfs),
            "the right side has no level");
    require(lost.silent_channels == std::vector<std::uint16_t>{1}, "the right side is named silent");
    std::vector<float> faint;
    sine(faint, rate, 440.0, -12.0, 2.0, 0.0, 1e-7);
    require(analyze_audio(faint, 2, rate).silent_channels == std::vector<std::uint16_t>{1},
            "a side 140 dB down is silent too");
    std::vector<float> both;
    sine(both, rate, 440.0, -12.0, 2.0);
    require(analyze_audio(both, 2, rate).silent_channels.empty(), "both sides sound");
    const std::vector<float> silence(static_cast<std::size_t>(rate) * 2, 0.0F);
    require(analyze_audio(silence, 2, rate).silent_channels.empty(),
            "all silence is silence, not a lost side");
}

// A tuner's reading: a harmonic tone 12 cents sharp of A3 reads +12, a chord
// of pure sines 20 cents flat reads -20, both with their peaks agreeing;
// noise has no agreed tuning and silence none at all.
void tuning_reads_the_offset() {
    for (const double rate : {48000.0, 44100.0}) {
        const auto frames = static_cast<std::size_t>(rate * 3.0);
        std::vector<float> tone(frames * 2, 0.0F), chord(frames * 2, 0.0F);
        const double a3 = 220.0 * std::pow(2.0, 12.0 / 1200.0);
        const double flat = std::pow(2.0, -20.0 / 1200.0);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const double t = static_cast<double>(frame) / rate;
            double harmonic = 0.0;
            for (int k = 1; k <= 4; k *= 2) harmonic += 0.3 / k * std::sin(2.0 * std::numbers::pi * a3 * k * t);
            tone[frame * 2] = tone[frame * 2 + 1] = static_cast<float>(harmonic);
            double triad = 0.0;
            for (const int key : {60, 64, 67})
                triad += 0.2 * std::sin(2.0 * std::numbers::pi * 440.0 * std::pow(2.0, (key - 69) / 12.0) * flat * t);
            chord[frame * 2] = chord[frame * 2 + 1] = static_cast<float>(triad);
        }
        const auto sharp = analyze_audio(tone, 2, rate);
        near(sharp.tuning_cents, 12.0, 1.0, "a harmonic A3 12 cents sharp");
        require(sharp.tuning_confidence > 0.95, "the tone's peaks agree");
        const auto low = analyze_audio(chord, 2, rate);
        near(low.tuning_cents, -20.0, 1.0, "a C major triad 20 cents flat");
        require(low.tuning_confidence > 0.95, "the chord's peaks agree");
    }
    const double rate = 48000.0;
    std::vector<float> noise(static_cast<std::size_t>(rate * 3.0) * 2);
    std::uint32_t state = 1;
    for (auto& sample : noise) {
        state = state * 1664525U + 1013904223U;
        sample = static_cast<float>((static_cast<double>(state >> 8) / 16777216.0 - 0.5) * 0.5);
    }
    require(analyze_audio(noise, 2, rate).tuning_confidence < 0.3, "noise has no agreed tuning");
    const std::vector<float> silence(static_cast<std::size_t>(rate) * 4, 0.0F);
    require(std::isnan(analyze_audio(silence, 2, rate).tuning_cents), "silence has no tuning");
}

}  // namespace

int main(int argc, char** argv) {
    const std::map<std::string, std::function<void()>> cases{
        {"loudness", sine_reads_its_loudness},
        {"gating", quiet_passages_are_gated},
        {"loudness_range", loudness_range_spans_the_passages},
        {"true_peak", true_peak_finds_the_crest_between_samples},
        {"bands", energy_sits_in_its_band},
        {"stereo_and_silence", stereo_image_and_silence},
        {"tuning", tuning_reads_the_offset},
        {"one_side_silent", one_side_silent_is_named},
    };
    if (argc != 2 || !cases.contains(argv[1])) {
        std::cerr << "usage: blokkily_analysis_tests <case>\n";
        return 2;
    }
    try {
        cases.at(argv[1])();
    } catch (const std::exception& error) {
        std::cerr << argv[1] << " failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << argv[1] << " passed\n";
    return 0;
}
