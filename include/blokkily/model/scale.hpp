#pragma once

#include "blokkily/model/tuning.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace blokkily {

// A scale, said in cents from its root rather than in semitones, so the same
// definition lands in any tuning: each interval is taken to the degree of the
// tuning that sounds closest to it.
struct Scale {
    std::string name = "Chromatic";
    std::vector<double> cents{}; // empty means every degree of the tuning
};

// One chord of a scale, as the degrees it sounds and the numeral it is known by.
struct ChordVoicing {
    std::vector<int> degrees;
    std::string name;
    std::string quality; // "major", "minor", "diminished", "augmented", "other"
};

[[nodiscard]] std::vector<Scale> scale_presets();
[[nodiscard]] std::optional<Scale> scale_by_name(std::string_view name);

// The degrees of one period that belong to the scale, ascending from its root.
[[nodiscard]] std::vector<int> scale_degrees(const Tuning& tuning, const Scale& scale,
                                             int root_degree);
[[nodiscard]] bool in_scale(const Tuning& tuning, const Scale& scale, int root_degree,
                            int degree);
// The scale degree closest to `degree`, which is what an auto-scale keyboard or
// a snapped edit plays instead of the note that was asked for.
[[nodiscard]] int snap_to_scale(const Tuning& tuning, const Scale& scale, int root_degree,
                                int degree);

// The chord built on a step of the scale: `voices` notes stacked in thirds of
// the scale itself, then inverted. Its quality is read back from the intervals
// it actually sounds, so a chord in 19-EDO is named by what it is.
[[nodiscard]] ChordVoicing scale_chord(const Tuning& tuning, const Scale& scale,
                                       int root_degree, int step, int voices = 3,
                                       int inversion = 0);

} // namespace blokkily
