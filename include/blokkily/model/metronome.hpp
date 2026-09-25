#pragma once

// The metronome and the count-in (item 3.7, decision 14). The click follows
// the song's tempo and meter maps: an accented click on every downbeat and a
// plainer one on every other beat of the meter's beat unit (a quarter in 4/4,
// an eighth in 7/8). It is heard live through a path of its own into the
// output, never through a song track, so no solo or mute reaches it, and it
// is left out of a bounce unless the export asks for it. A count-in plays
// `count_in_bars` bars of click before a recording starts, without moving the
// song position.
//
// These are how the session is set up to be played, not what the song plays:
// they are saved with the project (records_metronome.cpp) but a step of undo
// leaves them as they are, like arm and input state (decision 5).

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace blokkily {

struct MetronomeSettings {
    static constexpr int maximum_count_in_bars = 4;
    static constexpr double minimum_level_db = -60.0;
    static constexpr double maximum_level_db = 6.0;

    // The click is heard while the song plays.
    bool enabled = false;
    // The click's level: 0 dB puts an accented click at full scale.
    double level_db = -6.0;
    // Bars of click before a recording starts, 0 to 4. 0 starts at once.
    int count_in_bars = 0;

    [[nodiscard]] bool valid() const noexcept {
        return std::isfinite(level_db) && level_db >= minimum_level_db &&
               level_db <= maximum_level_db && count_in_bars >= 0 &&
               count_in_bars <= maximum_count_in_bars;
    }
    [[nodiscard]] static double clamp_level(double decibels) noexcept {
        if (!std::isfinite(decibels)) return MetronomeSettings{}.level_db;
        return std::clamp(decibels, minimum_level_db, maximum_level_db);
    }

    friend bool operator==(const MetronomeSettings&, const MetronomeSettings&) = default;
};

} // namespace blokkily
