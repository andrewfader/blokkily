// Comping and punch in the window (features/comping_and_punch.feature).
// Choosing a take and splitting the stack are steps of history; punch, like
// the loop whose IN and OUT it shares, is a session setting applied at once.

#include "song_model.hpp"

#include "blokkily/audio/comping.hpp"

void SongModel::setPunch(bool on) {
    // Punch needs a range: the loop's, set as the LOOP chip sets one.
    if (on && song_.loop.end <= song_.loop.start) {
        const bool was_looping = song_.loop.enabled;
        setLoopEnabled(true);
        song_.loop.enabled = was_looping;
    }
    if (song_.loop.punch == on) return;
    song_.loop.punch = on;
    emit loopChanged();
}

bool SongModel::chooseTake(qint64 id) {
    const auto* clip = findAudioClip(id);
    if (clip == nullptr) return false;
    if (!clip->comped_out) {
        // Already playing: nothing to choose unless an overlap is unchosen.
        bool other_playing = false;
        for (const auto other : blokkily::take_stack(song_, clip->id))
            if (other != clip->id && !findAudioClip(static_cast<qint64>(other))->comped_out)
                other_playing = true;
        if (!other_playing) return true;
    }
    checkpoint();
    (void)blokkily::choose_take(song_, static_cast<blokkily::AudioClipId>(id));
    notifyStructureChanged();
    emit audioClipsChanged();
    return true;
}

qint64 SongModel::nextTake(qint64 id) const {
    const auto* clip = findAudioClip(id);
    if (clip == nullptr) return -1;
    const auto stack = blokkily::take_stack(song_, clip->id);
    if (stack.size() < 2) return -1;
    // After the one that plays now, round to the first.
    std::size_t playing = 0;
    for (std::size_t index = 0; index < stack.size(); ++index)
        if (!findAudioClip(static_cast<qint64>(stack[index]))->comped_out) playing = index;
    return static_cast<qint64>(stack[(playing + 1) % stack.size()]);
}

int SongModel::splitTakes(int track, double tick) {
    if (!validTrack(track) || tick <= 0.0) return 0;
    auto changed = song_;
    const int cut = blokkily::split_takes(changed, static_cast<std::size_t>(track),
                                          static_cast<blokkily::Tick>(std::llround(tick)));
    if (cut == 0) return 0;
    checkpoint();
    song_ = std::move(changed);
    notifyStructureChanged();
    emit audioClipsChanged();
    return cut;
}
