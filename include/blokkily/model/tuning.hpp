#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace blokkily {

// A tuning system. Every tuning is said in cents, so equal divisions of the
// octave and rational tunings answer the same questions the editor asks: where
// a degree sits, what to call it, and what an instrument must be told to sound
// it. `degrees` holds one period, ascending, starting at zero; the period
// itself closes the list rather than appearing in it.
struct Tuning {
    std::string name = "12-EDO";
    std::vector<double> degrees{0, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000, 1100};
    double period_cents = 1200.0;
    // Degrees are counted from this key, so in twelve-tone equal temperament a
    // degree and a MIDI key are the same number and nothing has to be relearned.
    int anchor_key = 60;
    double anchor_hz = 261.6255653005986; // middle C against A440

    [[nodiscard]] int divisions() const noexcept {
        return static_cast<int>(degrees.size());
    }
    [[nodiscard]] bool is_twelve_tone() const;
};

// What an instrument is told: the nearest twelve-tone key and how far the pitch
// sits from it. Every plugin format speaks semitones plus a retune offset, so
// this is the shape the audio path carries.
struct TunedPitch {
    std::int16_t key = 60;
    double cents = 0.0;
};

[[nodiscard]] Tuning equal_division(int divisions, double period_cents = 1200.0,
                                    std::string name = {});
// The tunings the interface offers by name.
[[nodiscard]] std::vector<Tuning> tuning_presets();
[[nodiscard]] std::optional<Tuning> tuning_by_name(std::string_view name);
// A Scala-style list: one degree per line, either cents ("701.955") or a ratio
// ("3/2"). The last entry is the period, as Scala files write it.
[[nodiscard]] std::optional<Tuning> parse_tuning(std::string_view text, std::string name);

[[nodiscard]] double degree_cents(const Tuning& tuning, int degree);
[[nodiscard]] double degree_frequency(const Tuning& tuning, int degree);
[[nodiscard]] TunedPitch degree_pitch(const Tuning& tuning, int degree);
// The degree whose pitch lies closest to a twelve-tone key, so a pattern
// written in one tuning can still be read in another.
[[nodiscard]] int degree_for_key(const Tuning& tuning, int key);
// The degree that sounds a key-and-retune. A note is stored as the pitch an
// instrument is told, so this is how one already in a pattern is read back as
// a degree of the song's tuning and named by it.
[[nodiscard]] int degree_for_pitch(const Tuning& tuning, const TunedPitch& pitch);
[[nodiscard]] std::string degree_name(const Tuning& tuning, int degree);

// A twelve-tone key with a retune offset, as a frequency. This is the pitch the
// engine and the adapters must agree on.
[[nodiscard]] double pitch_frequency(const TunedPitch& pitch);

} // namespace blokkily
