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

}  // namespace

int main(int argc, char** argv) {
    const std::map<std::string, std::function<void()>> cases{
        {"loudness", sine_reads_its_loudness},
        {"gating", quiet_passages_are_gated},
        {"loudness_range", loudness_range_spans_the_passages},
        {"true_peak", true_peak_finds_the_crest_between_samples},
        {"bands", energy_sits_in_its_band},
        {"stereo_and_silence", stereo_image_and_silence},
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
