#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace blokkily {

// How loud a rendered mix is, and where its energy sits (features/
// render_analysis.feature). What a producer reads off a loudness meter and a
// spectrum analyser after printing a mix: integrated loudness and loudness
// range as ITU-R BS.1770-4 and EBU Tech 3342 define them, the true peak a
// converter will reconstruct, and the share of the energy in each band an
// engineer names when a mix is muddy, boomy or harsh.
//
// Levels that have nothing to measure (a silent file, or one shorter than a
// 400 ms block) are -infinity; ratios that cannot be formed are NaN.

struct BandEnergy {
    std::string name;
    double low_hz = 0.0;
    double high_hz = 0.0;
    // Mean square of the band alone, in dB: a full-scale sine inside the
    // band reads -3.01, as rms_dbfs does.
    double level_db = 0.0;
    // The band's part of all the energy between 20 Hz and 20 kHz, 0 to 1.
    double share = 0.0;
};

struct AudioAnalysis {
    double seconds = 0.0;
    double sample_rate = 0.0;
    std::uint16_t channels = 0;

    double integrated_lufs = 0.0;   // gated, BS.1770-4
    double momentary_max_lufs = 0.0;  // loudest 400 ms
    double short_term_max_lufs = 0.0; // loudest 3 s
    double short_term_max_at = 0.0;   // where that 3 s starts, in seconds
    double loudness_range_lu = 0.0;   // EBU Tech 3342

    double sample_peak_dbfs = 0.0;
    double true_peak_dbtp = 0.0;  // 4x oversampled
    double rms_dbfs = 0.0;        // mean square over every channel and sample
    double crest_db = 0.0;        // sample peak over RMS
    std::uint64_t clipped_samples = 0;  // samples at or past full scale

    double correlation = 0.0;  // left against right, -1 to 1; NaN for mono or silence
    double spectral_centroid_hz = 0.0;
    std::vector<BandEnergy> bands;
};

// Measures interleaved audio of `channels` channels (1 or 2 are weighted as
// BS.1770 weights left and right; any further channel counts alike).
[[nodiscard]] AudioAnalysis analyze_audio(std::span<const float> interleaved,
                                          std::uint16_t channels, double sample_rate);

} // namespace blokkily
