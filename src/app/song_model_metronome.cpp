// The metronome and count-in settings (item 3.7, decision 14). They are how
// the session is played, not what the song plays: saved with it, applied to
// the running engine at once, and never a step of history.

#include "song_model.hpp"

#include <algorithm>

void SongModel::setMetronomeOn(bool on) {
    if (song_.metronome.enabled == on) return;
    song_.metronome.enabled = on;
    emit metronomeChanged();
}

void SongModel::toggleMetronome() { setMetronomeOn(!song_.metronome.enabled); }

void SongModel::setMetronomeLevelDb(double decibels) {
    const double level = blokkily::MetronomeSettings::clamp_level(decibels);
    if (song_.metronome.level_db == level) return;
    song_.metronome.level_db = level;
    emit metronomeChanged();
}

void SongModel::setCountInBars(int bars) {
    const int counted = std::clamp(bars, 0, blokkily::MetronomeSettings::maximum_count_in_bars);
    if (song_.metronome.count_in_bars == counted) return;
    song_.metronome.count_in_bars = counted;
    emit metronomeChanged();
}

void SongModel::cycleCountIn(int step) {
    constexpr int choices = blokkily::MetronomeSettings::maximum_count_in_bars + 1;
    const int next = ((song_.metronome.count_in_bars + step) % choices + choices) % choices;
    setCountInBars(next);
}
