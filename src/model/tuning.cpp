#include "blokkily/model/tuning.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace blokkily {
namespace {

// Floor division and the matching remainder, so a degree below the anchor lands
// in the period below it rather than mirroring around zero.
int floor_div(int value, int divisor) {
    const int quotient = value / divisor;
    return (value % divisor != 0 && ((value < 0) != (divisor < 0))) ? quotient - 1 : quotient;
}
int positive_mod(int value, int divisor) { return value - floor_div(value, divisor) * divisor; }

std::string_view trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

std::optional<double> to_double(std::string_view text) {
    // from_chars for doubles is not available everywhere the project builds, so
    // the parse is done by hand against a strict grammar.
    try {
        std::size_t used = 0;
        const double value = std::stod(std::string(text), &used);
        if (used != text.size()) return std::nullopt;
        return value;
    } catch (...) {
        return std::nullopt;
    }
}

constexpr const char* sharp_names[] = {"C", "C#", "D", "D#", "E", "F",
                                       "F#", "G", "G#", "A", "A#", "B"};

} // namespace

bool Tuning::is_twelve_tone() const {
    if (divisions() != 12 || std::abs(period_cents - 1200.0) > 1e-6) return false;
    for (int degree = 0; degree < 12; ++degree)
        if (std::abs(degrees[static_cast<std::size_t>(degree)] - degree * 100.0) > 1e-6)
            return false;
    return true;
}

Tuning equal_division(int divisions, double period_cents, std::string name) {
    Tuning tuning;
    tuning.period_cents = period_cents;
    divisions = std::max(1, divisions);
    tuning.degrees.clear();
    tuning.degrees.reserve(static_cast<std::size_t>(divisions));
    for (int step = 0; step < divisions; ++step)
        tuning.degrees.push_back(step * period_cents / divisions);
    tuning.name = name.empty() ? std::to_string(divisions) + "-EDO" : std::move(name);
    return tuning;
}

std::vector<Tuning> tuning_presets() {
    std::vector<Tuning> presets;
    presets.push_back(equal_division(12));
    presets.push_back(equal_division(19));
    presets.push_back(equal_division(24, 1200.0, "24-EDO quarter tones"));
    presets.push_back(equal_division(31));
    presets.push_back(equal_division(53));
    // Bohlen-Pierce divides a perfect twelfth instead of an octave, so the
    // period is a tuning's own business rather than an assumption.
    presets.push_back(equal_division(13, 1901.955, "Bohlen-Pierce"));

    Tuning just;
    just.name = "Just intonation (5-limit)";
    just.degrees = {0.0, 111.731, 203.910, 315.641, 386.314, 498.045,
                    590.224, 701.955, 813.686, 884.359, 996.090, 1088.269};
    presets.push_back(just);

    Tuning pythagorean;
    pythagorean.name = "Pythagorean";
    pythagorean.degrees = {0.0, 113.685, 203.910, 294.135, 407.820, 498.045,
                           611.730, 701.955, 815.640, 905.865, 996.090, 1109.775};
    presets.push_back(pythagorean);

    Tuning meantone;
    meantone.name = "Quarter-comma meantone";
    meantone.degrees = {0.0, 76.049, 193.157, 310.265, 386.314, 503.422,
                        579.471, 696.578, 772.627, 889.735, 1006.843, 1082.892};
    presets.push_back(meantone);
    return presets;
}

std::optional<Tuning> tuning_by_name(std::string_view name) {
    for (auto& tuning : tuning_presets())
        if (tuning.name == name) return tuning;
    return std::nullopt;
}

std::optional<Tuning> parse_tuning(std::string_view text, std::string name) {
    std::vector<double> cents{0.0};
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = std::min(text.find('\n', start), text.size());
        const auto line = trim(text.substr(start, end - start));
        start = end + 1;
        if (line.empty() || line.front() == '!') continue; // Scala comments
        if (const auto slash = line.find('/'); slash != std::string_view::npos) {
            const auto numerator = to_double(trim(line.substr(0, slash)));
            const auto denominator = to_double(trim(line.substr(slash + 1)));
            if (!numerator || !denominator || *denominator == 0.0 || *numerator <= 0.0)
                return std::nullopt;
            cents.push_back(1200.0 * std::log2(*numerator / *denominator));
            continue;
        }
        const auto value = to_double(line);
        if (!value) return std::nullopt;
        cents.push_back(*value);
    }
    // A tuning needs a period and at least one degree inside it.
    if (cents.size() < 2) return std::nullopt;
    Tuning tuning;
    tuning.period_cents = cents.back();
    cents.pop_back();
    if (tuning.period_cents <= 0.0) return std::nullopt;
    if (!std::is_sorted(cents.begin(), cents.end())) return std::nullopt;
    tuning.degrees = std::move(cents);
    tuning.name = std::move(name);
    return tuning;
}

double degree_cents(const Tuning& tuning, int degree) {
    const int divisions = std::max(1, tuning.divisions());
    const int relative = degree - tuning.anchor_key;
    const int periods = floor_div(relative, divisions);
    const int index = positive_mod(relative, divisions);
    return periods * tuning.period_cents + tuning.degrees[static_cast<std::size_t>(index)];
}

double degree_frequency(const Tuning& tuning, int degree) {
    return tuning.anchor_hz * std::pow(2.0, degree_cents(tuning, degree) / 1200.0);
}

TunedPitch degree_pitch(const Tuning& tuning, int degree) {
    const double cents = degree_cents(tuning, degree);
    const int semitones = static_cast<int>(std::llround(cents / 100.0));
    TunedPitch pitch;
    pitch.key = static_cast<std::int16_t>(std::clamp(tuning.anchor_key + semitones, 0, 127));
    // What the nearest key cannot say, the offset does. Clamping the key would
    // otherwise silently move the pitch, so the remainder is measured from the
    // key that will actually be sent.
    pitch.cents = cents - (pitch.key - tuning.anchor_key) * 100.0;
    return pitch;
}

namespace {
// The degree whose pitch lies closest to a position said in cents above the
// anchor. Both lookups below are the same search over a different starting
// point, so the rounding rule lives in one place.
int nearest_degree(const Tuning& tuning, double wanted) {
    const int divisions = std::max(1, tuning.divisions());
    const int guess = tuning.anchor_key +
                      static_cast<int>(std::llround(wanted / (tuning.period_cents / divisions)));
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
} // namespace

int degree_for_key(const Tuning& tuning, int key) {
    return nearest_degree(tuning, (key - tuning.anchor_key) * 100.0);
}

int degree_for_pitch(const Tuning& tuning, const TunedPitch& pitch) {
    return nearest_degree(tuning, (pitch.key - tuning.anchor_key) * 100.0 + pitch.cents);
}

std::string degree_name(const Tuning& tuning, int degree) {
    const int divisions = std::max(1, tuning.divisions());
    const int relative = degree - tuning.anchor_key;
    const int period = floor_div(relative, divisions);
    const int index = positive_mod(relative, divisions);
    // Middle C is C4, so the anchor's period is the fourth octave.
    const int octave = period + 4;
    if (tuning.is_twelve_tone())
        return std::string(sharp_names[index]) + std::to_string(octave);
    // Outside twelve tones a letter would be a lie, so degrees are named the
    // way microtonal music names them: step count over the division.
    return std::to_string(index) + "\\" + std::to_string(divisions) + "." +
           std::to_string(octave);
}

double pitch_frequency(const TunedPitch& pitch) {
    return 440.0 * std::pow(2.0, (pitch.key - 69) / 12.0) * std::pow(2.0, pitch.cents / 1200.0);
}

} // namespace blokkily
