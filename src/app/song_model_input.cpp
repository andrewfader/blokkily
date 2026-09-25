// Track arm and input (plan item 2.5; decisions 3, 4 and 5). A track's R arms
// it for notes (and, with item 3.2, audio); its channel says which of a MIDI
// keyboard's sixteen channels it hears. Both live on the track, so they are
// saved with the song, but they are how the session is wired for recording,
// not the music: arming is never a step of history, and taking a step back or
// forward leaves every track armed the way it is now.

#include "song_model.hpp"

#include "blokkily/audio/event_queue.hpp"

#include <QtGlobal>

#include <algorithm>

namespace {

// Whether two tracks are the same mixer track seen at two moments of history:
// the same name playing the same instrument.
bool same_track(const blokkily::Track& a, const blokkily::Track& b) {
    return a.name == b.name && a.instrument.format == b.instrument.format &&
           a.instrument.path == b.instrument.path &&
           a.instrument.identifier == b.instrument.identifier;
}

} // namespace

void SongModel::keepInputs(const blokkily::Song& live, blokkily::Song& restored) {
    restored.record_offset_samples = live.record_offset_samples;
    const auto& now = live.tracks;
    auto& then = restored.tracks;
    if (now.size() == then.size()) {
        for (std::size_t index = 0; index < now.size(); ++index)
            then[index].input = now[index].input;
        return;
    }
    // A step that added or removed a track: the tracks before the first one
    // that differs are the same tracks, and so are the tracks after it counted
    // from the end. A track that only one side has keeps what it had.
    const auto shorter = std::min(now.size(), then.size());
    std::size_t prefix = 0;
    while (prefix < shorter && same_track(now[prefix], then[prefix])) ++prefix;
    for (std::size_t index = 0; index < prefix; ++index) then[index].input = now[index].input;
    std::size_t suffix = 0;
    while (suffix < shorter - prefix &&
           same_track(now[now.size() - 1 - suffix], then[then.size() - 1 - suffix])) {
        then[then.size() - 1 - suffix].input = now[now.size() - 1 - suffix].input;
        ++suffix;
    }
}

void SongModel::setArmed(int track, bool armed) {
    if (!validTrack(track)) return;
    auto& input = song_.tracks[static_cast<std::size_t>(track)].input;
    if (input.armed == armed) return;
    input.armed = armed;
    emit inputChanged();
    emit songChanged();
}

void SongModel::toggleArm(int track) {
    if (!validTrack(track)) return;
    setArmed(track, !song_.tracks[static_cast<std::size_t>(track)].input.armed);
}

void SongModel::setInputChannel(int track, int channel) {
    if (!validTrack(track)) return;
    auto& input = song_.tracks[static_cast<std::size_t>(track)].input;
    const auto wanted = static_cast<std::int8_t>(qBound(0, channel, 16) - 1);
    if (input.midi_channel == wanted) return;
    input.midi_channel = wanted;
    emit inputChanged();
    emit songChanged();
}

void SongModel::cycleInputChannel(int track, int step) {
    if (!validTrack(track)) return;
    const int current = song_.tracks[static_cast<std::size_t>(track)].input.midi_channel + 1;
    // ALL, 1 .. 16, and round again.
    setInputChannel(track, ((current + step) % 17 + 17) % 17);
}

int SongModel::armedCount() const {
    int armed = 0;
    for (const auto& track : song_.tracks) armed += track.input.armed ? 1 : 0;
    return armed;
}
