#include "blokkily/audio/analysis.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>

namespace blokkily {
namespace {

constexpr double minus_infinity = -std::numeric_limits<double>::infinity();
constexpr double not_a_number = std::numeric_limits<double>::quiet_NaN();

double power_db(double mean_square) {
    return mean_square > 0.0 ? 10.0 * std::log10(mean_square) : minus_infinity;
}

// BS.1770's loudness of a block from each channel's mean square of the
// K-weighted signal; every channel here has the weight 1 that left and right
// have.
double block_loudness(double summed_mean_square) {
    return summed_mean_square > 0.0 ? -0.691 + 10.0 * std::log10(summed_mean_square)
                                    : minus_infinity;
}

// One biquad, direct form I, in double precision.
struct Biquad {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    double operator()(double x) {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1;
        x1 = x;
        y2 = y1;
        y1 = y;
        return y;
    }
};

// The K-weighting of BS.1770-4: a high shelf for the head, then the RLB high
// pass. The standard tabulates both at 48 kHz; these are the analogue
// prototypes its coefficients come from, so any rate is weighted alike.
std::array<Biquad, 2> k_weighting(double rate) {
    std::array<Biquad, 2> stages;
    {
        const double f0 = 1681.974450955533;
        const double gain_db = 3.999843853973347;
        const double q = 0.7071752369554196;
        const double k = std::tan(std::numbers::pi * f0 / rate);
        const double vh = std::pow(10.0, gain_db / 20.0);
        const double vb = std::pow(vh, 0.4996667741545416);
        const double a0 = 1.0 + k / q + k * k;
        auto& shelf = stages[0];
        shelf.b0 = (vh + vb * k / q + k * k) / a0;
        shelf.b1 = 2.0 * (k * k - vh) / a0;
        shelf.b2 = (vh - vb * k / q + k * k) / a0;
        shelf.a1 = 2.0 * (k * k - 1.0) / a0;
        shelf.a2 = (1.0 - k / q + k * k) / a0;
    }
    {
        const double f0 = 38.13547087602444;
        const double q = 0.5003270373238773;
        const double k = std::tan(std::numbers::pi * f0 / rate);
        const double a0 = 1.0 + k / q + k * k;
        auto& high_pass = stages[1];
        high_pass.b0 = 1.0;
        high_pass.b1 = -2.0;
        high_pass.b2 = 1.0;
        high_pass.a1 = 2.0 * (k * k - 1.0) / a0;
        high_pass.a2 = (1.0 - k / q + k * k) / a0;
    }
    return stages;
}

// The loudest of the values the 4x-oversampled signal passes through between
// samples, by windowed-sinc interpolation (BS.1770-4 annex 2). Phase 0 is the
// sample itself.
class TruePeak {
public:
    TruePeak() {
        for (std::size_t phase = 1; phase < 4; ++phase) {
            const double offset = static_cast<double>(phase) / 4.0;
            double sum = 0.0;
            for (std::size_t tap = 0; tap < taps; ++tap) {
                // Sample index relative to the one before the point: -7 .. 8.
                const double t = offset - (static_cast<double>(tap) - (half - 1.0));
                const double sinc = std::sin(std::numbers::pi * t) / (std::numbers::pi * t);
                const double x = t / half;
                const double window = 0.42 + 0.5 * std::cos(std::numbers::pi * x) +
                                      0.08 * std::cos(2.0 * std::numbers::pi * x);
                kernel_[phase][tap] = sinc * window;
                sum += kernel_[phase][tap];
            }
            // Each phase passes DC at unity.
            for (auto& weight : kernel_[phase]) weight /= sum;
        }
    }

    // The true peak of one channel of interleaved audio.
    double of(std::span<const float> interleaved, std::size_t channels, std::size_t channel) const {
        const std::size_t frames = interleaved.size() / channels;
        // The channel alone, with silence either side for the kernel to reach.
        const auto before = static_cast<std::size_t>(half) - 1;
        std::vector<double> padded(frames + taps, 0.0);
        for (std::size_t frame = 0; frame < frames; ++frame)
            padded[before + frame] = interleaved[frame * channels + channel];
        double peak = 0.0;
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const double* window = padded.data() + frame;
            peak = std::max(peak, std::abs(window[before]));
            for (std::size_t phase = 1; phase < 4; ++phase) {
                const double* weights = kernel_[phase].data();
                double value = 0.0;
                for (std::size_t tap = 0; tap < taps; ++tap) value += weights[tap] * window[tap];
                peak = std::max(peak, std::abs(value));
            }
        }
        return peak;
    }

private:
    static constexpr std::size_t taps = 16;
    static constexpr double half = taps / 2.0;
    std::array<std::array<double, taps>, 4> kernel_{};
};

// In-place radix-2 FFT of a power-of-two length, real and imaginary parts
// apart and through bare pointers, so a debug build is not slowed by
// std::complex's NaN guards or checked indexing on every butterfly.
void fft(double* re, double* im, std::size_t n, const double* cos_table, const double* sin_table) {
    for (std::size_t i = 1, j = 0; i < n; ++i) {
        std::size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            std::swap(re[i], re[j]);
            std::swap(im[i], im[j]);
        }
    }
    for (std::size_t length = 2; length <= n; length <<= 1) {
        const std::size_t half = length / 2;
        const std::size_t stride = n / length;
        for (std::size_t start = 0; start < n; start += length)
            for (std::size_t k = 0; k < half; ++k) {
                const std::size_t a = start + k;
                const std::size_t b = a + half;
                const double wr = cos_table[k * stride];
                const double wi = sin_table[k * stride];
                const double odd_re = re[b] * wr - im[b] * wi;
                const double odd_im = re[b] * wi + im[b] * wr;
                re[b] = re[a] - odd_re;
                im[b] = im[a] - odd_im;
                re[a] += odd_re;
                im[a] += odd_im;
            }
    }
}

// The long-term power spectrum, by Welch's method: Hann windows overlapping
// by half, every channel's power added. Bin k holds the mean square of the
// signal in that bin, so the bins sum to the signal's mean square.
std::vector<double> power_spectrum(std::span<const float> interleaved, std::size_t channels,
                                   std::size_t size) {
    std::vector<double> window(size);
    double window_power = 0.0;
    for (std::size_t i = 0; i < size; ++i) {
        window[i] = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * static_cast<double>(i) /
                                         static_cast<double>(size));
        window_power += window[i] * window[i];
    }
    std::vector<double> cos_table(size / 2), sin_table(size / 2);
    for (std::size_t k = 0; k < size / 2; ++k) {
        const double angle = -2.0 * std::numbers::pi * static_cast<double>(k) / static_cast<double>(size);
        cos_table[k] = std::cos(angle);
        sin_table[k] = std::sin(angle);
    }
    const std::size_t frames = interleaved.size() / channels;
    std::vector<double> spectrum(size / 2 + 1, 0.0);
    std::vector<double> re(size), im(size);
    std::size_t segments = 0;
    const std::size_t hop = size / 2;
    for (std::size_t start = 0; start == 0 || start + size <= frames; start += hop) {
        for (std::size_t channel = 0; channel < channels; ++channel) {
            double* real = re.data();
            double* imaginary = im.data();
            const double* weights = window.data();
            const float* samples = interleaved.data();
            for (std::size_t i = 0; i < size; ++i) {
                const std::size_t frame = start + i;
                const double sample = frame < frames ? samples[frame * channels + channel] : 0.0;
                real[i] = sample * weights[i];
                imaginary[i] = 0.0;
            }
            fft(real, imaginary, size, cos_table.data(), sin_table.data());
            double* bins = spectrum.data();
            for (std::size_t k = 0; k <= size / 2; ++k) {
                // One-sided: every bin but DC and Nyquist stands for two.
                const double scale = (k == 0 || k == size / 2) ? 1.0 : 2.0;
                bins[k] += scale * (real[k] * real[k] + imaginary[k] * imaginary[k]);
            }
        }
        ++segments;
        if (start + size >= frames) break;
    }
    // Parseval: the bins of one window sum to size * sum(x^2 w^2); divided
    // by size * sum(w^2) they sum to the mean square the window saw.
    const double scale = 1.0 / (static_cast<double>(size) * window_power * static_cast<double>(segments));
    for (auto& bin : spectrum) bin *= scale;
    return spectrum;
}

struct BandEdges {
    const char* name;
    double low;
    double high;
};

constexpr std::array<BandEdges, 7> band_edges{{
    {"sub", 20.0, 60.0},
    {"bass", 60.0, 250.0},
    {"lowMid", 250.0, 500.0},
    {"mid", 500.0, 2000.0},
    {"highMid", 2000.0, 4000.0},
    {"presence", 4000.0, 6000.0},
    {"air", 6000.0, 20000.0},
}};

// EBU Tech 3342's loudness range of the short-term loudnesses.
double loudness_range(std::vector<double> short_term) {
    std::erase_if(short_term, [](double loudness) { return !(loudness > -70.0); });
    if (short_term.empty()) return 0.0;
    double mean_square = 0.0;
    for (double loudness : short_term) mean_square += std::pow(10.0, (loudness + 0.691) / 10.0);
    mean_square /= static_cast<double>(short_term.size());
    const double relative_gate = block_loudness(mean_square) - 20.0;
    std::erase_if(short_term, [relative_gate](double loudness) { return loudness < relative_gate; });
    if (short_term.empty()) return 0.0;
    std::sort(short_term.begin(), short_term.end());
    const auto percentile = [&short_term](double fraction) {
        const double position = fraction * static_cast<double>(short_term.size() - 1);
        const auto below = static_cast<std::size_t>(std::floor(position));
        const auto above = std::min(below + 1, short_term.size() - 1);
        return short_term[below] + (short_term[above] - short_term[below]) *
                                       (position - static_cast<double>(below));
    };
    return percentile(0.95) - percentile(0.10);
}

}  // namespace

AudioAnalysis analyze_audio(std::span<const float> interleaved, std::uint16_t channel_count,
                            double sample_rate) {
    AudioAnalysis result;
    result.sample_rate = sample_rate;
    result.channels = channel_count;
    const std::size_t channels = std::max<std::size_t>(1, channel_count);
    const std::size_t frames = interleaved.size() / channels;
    result.seconds = sample_rate > 0.0 ? static_cast<double>(frames) / sample_rate : 0.0;
    result.integrated_lufs = result.momentary_max_lufs = result.short_term_max_lufs = minus_infinity;
    result.sample_peak_dbfs = result.true_peak_dbtp = result.rms_dbfs = minus_infinity;
    result.crest_db = result.correlation = not_a_number;
    result.spectral_centroid_hz = not_a_number;
    if (frames == 0 || sample_rate <= 0.0) return result;

    // Levels of the samples themselves.
    double peak = 0.0;
    double sum_square = 0.0;
    double left_square = 0.0, right_square = 0.0, cross = 0.0;
    for (std::size_t frame = 0; frame < frames; ++frame)
        for (std::size_t channel = 0; channel < channels; ++channel) {
            const double sample = interleaved[frame * channels + channel];
            peak = std::max(peak, std::abs(sample));
            sum_square += sample * sample;
            if (std::abs(sample) >= 1.0) ++result.clipped_samples;
        }
    if (channels >= 2)
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const double left = interleaved[frame * channels];
            const double right = interleaved[frame * channels + 1];
            left_square += left * left;
            right_square += right * right;
            cross += left * right;
        }
    const double mean_square = sum_square / static_cast<double>(frames * channels);
    result.sample_peak_dbfs = peak > 0.0 ? 20.0 * std::log10(peak) : minus_infinity;
    result.rms_dbfs = power_db(mean_square);
    if (peak > 0.0) result.crest_db = result.sample_peak_dbfs - result.rms_dbfs;
    if (left_square > 0.0 && right_square > 0.0)
        result.correlation = cross / std::sqrt(left_square * right_square);

    const TruePeak true_peak;
    double true_peak_linear = 0.0;
    for (std::size_t channel = 0; channel < channels; ++channel)
        true_peak_linear = std::max(true_peak_linear, true_peak.of(interleaved, channels, channel));
    result.true_peak_dbtp = true_peak_linear > 0.0 ? 20.0 * std::log10(true_peak_linear) : minus_infinity;

    // Loudness: the K-weighted energy of every 100 ms, from which the 400 ms
    // momentary blocks (75% overlap) and 3 s short-term windows are summed.
    const auto hop = static_cast<std::size_t>(std::max(1.0, std::round(sample_rate / 10.0)));
    const std::size_t hops = frames / hop;
    std::vector<double> hop_energy(hops, 0.0);
    for (std::size_t channel = 0; channel < channels; ++channel) {
        auto filters = k_weighting(sample_rate);
        for (std::size_t frame = 0; frame < hops * hop; ++frame) {
            const double weighted =
                filters[1](filters[0](interleaved[frame * channels + channel]));
            hop_energy[frame / hop] += weighted * weighted;
        }
    }
    const auto window_mean_squares = [&](std::size_t length) {
        std::vector<double> windows;
        if (hops < length) return windows;
        double running = 0.0;
        for (std::size_t i = 0; i < length; ++i) running += hop_energy[i];
        windows.push_back(running / static_cast<double>(length * hop));
        for (std::size_t end = length; end < hops; ++end) {
            running += hop_energy[end] - hop_energy[end - length];
            windows.push_back(std::max(0.0, running) / static_cast<double>(length * hop));
        }
        return windows;
    };
    const auto momentary = window_mean_squares(4);
    std::vector<double> gated;
    for (double block : momentary) {
        const double loudness = block_loudness(block);
        result.momentary_max_lufs = std::max(result.momentary_max_lufs, loudness);
        if (loudness > -70.0) gated.push_back(block);
    }
    if (!gated.empty()) {
        double sum = 0.0;
        for (double block : gated) sum += block;
        const double relative_gate = block_loudness(sum / static_cast<double>(gated.size())) - 10.0;
        double kept = 0.0;
        std::size_t count = 0;
        for (double block : gated)
            if (block_loudness(block) > relative_gate) {
                kept += block;
                ++count;
            }
        if (count > 0) result.integrated_lufs = block_loudness(kept / static_cast<double>(count));
    }
    std::vector<double> short_term;
    const auto short_windows = window_mean_squares(30);
    for (std::size_t index = 0; index < short_windows.size(); ++index) {
        const double loudness = block_loudness(short_windows[index]);
        short_term.push_back(loudness);
        if (loudness > result.short_term_max_lufs) {
            result.short_term_max_lufs = loudness;
            result.short_term_max_at = static_cast<double>(index * hop) / sample_rate;
        }
    }
    result.loudness_range_lu = loudness_range(std::move(short_term));

    // Where the energy sits. 4096 points resolve about 12 Hz at 48 kHz,
    // enough to keep the sub band (20-60 Hz) apart from the bass.
    std::size_t size = 4096;
    while (size < sample_rate / 12.0) size *= 2;
    const auto spectrum = power_spectrum(interleaved, channels, size);
    const double bin_hz = sample_rate / static_cast<double>(size);
    const double nyquist = sample_rate / 2.0;
    const auto energy_between = [&](double low, double high) {
        double energy = 0.0;
        for (std::size_t k = 0; k < spectrum.size(); ++k) {
            const double frequency = static_cast<double>(k) * bin_hz;
            if (frequency >= low && frequency < high) energy += spectrum[k];
        }
        return energy;
    };
    const double audible = energy_between(20.0, std::min(20000.0, nyquist + bin_hz));
    double weighted_frequency = 0.0;
    for (std::size_t k = 0; k < spectrum.size(); ++k) {
        const double frequency = static_cast<double>(k) * bin_hz;
        if (frequency >= 20.0 && frequency < 20000.0) weighted_frequency += frequency * spectrum[k];
    }
    if (audible > 0.0) result.spectral_centroid_hz = weighted_frequency / audible;
    for (const auto& edges : band_edges) {
        if (edges.low >= nyquist) break;
        const double high = std::min(edges.high, nyquist + bin_hz);
        // The spectrum adds every channel's power; a band's level is a
        // channel's mean, like rms_dbfs.
        const double energy = energy_between(edges.low, high);
        result.bands.push_back({edges.name, edges.low, std::min(edges.high, nyquist),
                                power_db(energy / static_cast<double>(channels)),
                                audible > 0.0 ? energy / audible : 0.0});
    }
    return result;
}

} // namespace blokkily
