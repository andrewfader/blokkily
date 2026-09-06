#include "blokkily/model/scale.hpp"

#include <algorithm>
#include <cmath>

namespace blokkily {
namespace {

// The degree of `tuning` whose pitch lies closest to `cents` above the root.
int nearest_degree(const Tuning& tuning, int root_degree, double cents) {
    const double wanted = degree_cents(tuning, root_degree) + cents;
    const int divisions = std::max(1, tuning.divisions());
    const int guess = root_degree +
                      static_cast<int>(std::llround(cents / (tuning.period_cents / divisions)));
    int best = guess;
    double best_distance = std::abs(degree_cents(tuning, guess) - wanted);
    for (int offset = -divisions; offset <= divisions; ++offset) {
        const int candidate = guess + offset;
        const double distance = std::abs(degree_cents(tuning, candidate) - wanted);
        if (distance < best_distance) {
            best = candidate;
            best_distance = distance;
        }
    }
    return best;
}

const char* quality_of(double third, double fifth) {
    const bool minor_third = std::abs(third - 300.0) < 60.0;
    const bool major_third = std::abs(third - 400.0) < 60.0;
    const bool perfect_fifth = std::abs(fifth - 700.0) < 60.0;
    if (major_third && perfect_fifth) return "major";
    if (minor_third && perfect_fifth) return "minor";
    if (minor_third && fifth < 660.0) return "diminished";
    if (major_third && fifth > 740.0) return "augmented";
    return "other";
}

std::string numeral(int step, std::string_view quality, int voices) {
    static constexpr const char* upper[] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static constexpr const char* lower[] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
    const int index = ((step % 7) + 7) % 7;
    std::string name = (quality == "minor" || quality == "diminished") ? lower[index]
                                                                      : upper[index];
    if (quality == "diminished") name += "°";
    if (quality == "augmented") name += "+";
    if (voices >= 4) name += "7";
    return name;
}

} // namespace

std::vector<Scale> scale_presets() {
    return {
        {"Chromatic", {}},
        {"Major", {0, 200, 400, 500, 700, 900, 1100}},
        {"Natural minor", {0, 200, 300, 500, 700, 800, 1000}},
        {"Harmonic minor", {0, 200, 300, 500, 700, 800, 1100}},
        {"Melodic minor", {0, 200, 300, 500, 700, 900, 1100}},
        {"Dorian", {0, 200, 300, 500, 700, 900, 1000}},
        {"Phrygian", {0, 100, 300, 500, 700, 800, 1000}},
        {"Lydian", {0, 200, 400, 600, 700, 900, 1100}},
        {"Mixolydian", {0, 200, 400, 500, 700, 900, 1000}},
        {"Locrian", {0, 100, 300, 500, 600, 800, 1000}},
        {"Major pentatonic", {0, 200, 400, 700, 900}},
        {"Minor pentatonic", {0, 300, 500, 700, 1000}},
        {"Blues", {0, 300, 500, 600, 700, 1000}},
        {"Whole tone", {0, 200, 400, 600, 800, 1000}},
        {"Hirajoshi", {0, 200, 300, 700, 800}},
        {"Maqam Rast", {0, 200, 350, 500, 700, 900, 1050}},
        {"Maqam Bayati", {0, 150, 300, 500, 700, 800, 1000}},
    };
}

std::optional<Scale> scale_by_name(std::string_view name) {
    for (auto& scale : scale_presets())
        if (scale.name == name) return scale;
    return std::nullopt;
}

std::vector<int> scale_degrees(const Tuning& tuning, const Scale& scale, int root_degree) {
    std::vector<int> degrees;
    if (scale.cents.empty()) {
        for (int step = 0; step < std::max(1, tuning.divisions()); ++step)
            degrees.push_back(root_degree + step);
        return degrees;
    }
    for (const double cents : scale.cents) {
        const int degree = nearest_degree(tuning, root_degree, cents);
        // Two intervals can land on the same degree in a coarse tuning; the
        // scale then has fewer notes there rather than a repeated one.
        if (degrees.empty() || degrees.back() != degree) degrees.push_back(degree);
    }
    return degrees;
}

bool in_scale(const Tuning& tuning, const Scale& scale, int root_degree, int degree) {
    if (scale.cents.empty()) return true;
    const int divisions = std::max(1, tuning.divisions());
    const auto degrees = scale_degrees(tuning, scale, root_degree);
    for (const int member : degrees) {
        const int distance = degree - member;
        if (distance % divisions == 0) return true;
    }
    return false;
}

int snap_to_scale(const Tuning& tuning, const Scale& scale, int root_degree, int degree) {
    if (scale.cents.empty()) return degree;
    const int divisions = std::max(1, tuning.divisions());
    const auto degrees = scale_degrees(tuning, scale, root_degree);
    int best = degree;
    int best_distance = divisions * 2;
    for (const int member : degrees)
        for (int period = -2; period <= 2; ++period) {
            const int candidate = member + period * divisions;
            const int distance = std::abs(candidate - degree);
            // A tie moves upward, so snapping is predictable rather than
            // dependent on which member happened to be tested first.
            if (distance < best_distance || (distance == best_distance && candidate > best)) {
                best = candidate;
                best_distance = distance;
            }
        }
    return best;
}

ChordVoicing scale_chord(const Tuning& tuning, const Scale& scale, int root_degree, int step,
                         int voices, int inversion) {
    ChordVoicing chord;
    const auto degrees = scale_degrees(tuning, scale, root_degree);
    if (degrees.empty()) return chord;
    const int size = static_cast<int>(degrees.size());
    const int divisions = std::max(1, tuning.divisions());
    voices = std::clamp(voices, 2, 6);
    // Stacked in thirds of the scale, which is what makes the chord diatonic in
    // whatever tuning and mode the producer is in.
    for (int voice = 0; voice < voices; ++voice) {
        const int index = step + voice * 2;
        const int period = (index >= 0) ? index / size : -((-index + size - 1) / size);
        const int wrapped = ((index % size) + size) % size;
        chord.degrees.push_back(degrees[static_cast<std::size_t>(wrapped)] + period * divisions);
    }
    for (int turn = 0; turn < std::max(0, inversion) && !chord.degrees.empty(); ++turn) {
        const int lowest = chord.degrees.front();
        chord.degrees.erase(chord.degrees.begin());
        chord.degrees.push_back(lowest + divisions);
    }
    std::sort(chord.degrees.begin(), chord.degrees.end());
    const double root = degree_cents(tuning, chord.degrees.front());
    const double third = chord.degrees.size() > 1
                             ? degree_cents(tuning, chord.degrees[1]) - root : 0.0;
    const double fifth = chord.degrees.size() > 2
                             ? degree_cents(tuning, chord.degrees[2]) - root : 0.0;
    chord.quality = quality_of(third, fifth);
    chord.name = numeral(step, chord.quality, voices);
    return chord;
}

} // namespace blokkily
