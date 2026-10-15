// The arrangement loop: where playback wraps, set from the LOOP chip and
// the ruler. Saved with the song, applied to the running engine at once
// (loopChanged), never a step of history.

#include "song_model.hpp"

#include <algorithm>

int SongModel::loopStartBar() const {
    return barAt(song_.loop.start);
}

int SongModel::loopEndBar() const {
    if (song_.loop.end <= 0) return loopStartBar();
    const int at = barAt(song_.loop.end);
    return song_.meter.bar_start(at) == song_.loop.end ? at : at + 1;
}

void SongModel::setLoopEnabled(bool on) {
    if (on && song_.loop.end <= song_.loop.start) {
        song_.loop.start = 0;
        const int last = std::max(1, std::min(4, bars() - 1));
        song_.loop.end = barStart(last);
        if (song_.loop.end <= song_.loop.start) song_.loop.end = barStart(1);
    }
    if (song_.loop.enabled == on) return;
    song_.loop.enabled = on;
    emit loopChanged();
}

void SongModel::toggleLoop() { setLoopEnabled(!song_.loop.enabled); }

void SongModel::setLoopStartBar(int bar) {
    if (bar < 0) return;
    const auto start = barStart(bar);
    auto end = song_.loop.end;
    if (end <= start) end = barStart(bar + 1);
    if (song_.loop.start == start && song_.loop.end == end && song_.loop.enabled) return;
    song_.loop.start = start;
    song_.loop.end = end;
    song_.loop.enabled = true;
    emit loopChanged();
}

void SongModel::setLoopEndBar(int bar) {
    if (bar <= 0) return;
    const auto end = barStart(bar);
    auto start = song_.loop.start;
    if (end <= start) {
        const int from = std::max(0, bar - 1);
        start = barStart(from);
    }
    if (end <= start) return;
    if (song_.loop.start == start && song_.loop.end == end && song_.loop.enabled) return;
    song_.loop.start = start;
    song_.loop.end = end;
    song_.loop.enabled = true;
    emit loopChanged();
}
