#include "song_model.hpp"

#include "blokkily/audio/mixer.hpp"

#include <QFileInfo>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

const blokkily::Clip* SongModel::clipAt(int track, int bar) const {
    if (!validTrack(track) || bar < 0) return nullptr;
    const auto tick = static_cast<blokkily::Tick>(bar) * ticks_per_bar;
    const auto found = std::find_if(song_.clips.begin(), song_.clips.end(),
        [&](const blokkily::Clip& clip) {
            if (clip.track != static_cast<std::size_t>(track)) return false;
            if (clip.pattern >= song_.patterns.size()) return false;
            const auto span = song_.patterns[clip.pattern].pattern.length() *
                static_cast<blokkily::Tick>(std::max<std::uint32_t>(1, clip.repeats));
            return tick >= clip.start && tick < clip.start + span;
        });
    return found == song_.clips.end() ? nullptr : &*found;
}

blokkily::Clip* SongModel::clipAt(int track, int bar) {
    return const_cast<blokkily::Clip*>(
        static_cast<const SongModel*>(this)->clipAt(track, bar));
}

bool SongModel::hasClip(int track, int bar) const { return clipAt(track, bar) != nullptr; }

QVariantList SongModel::lanes() const {
    QVariantList all;
    const int bar_count = bars();
    for (int track = 0; track < trackCount(); ++track) {
        QVariantList lane;
        for (int bar = 0; bar < bar_count; ++bar) {
            const auto* clip = clipAt(track, bar);
            QVariantMap cell;
            cell["filled"] = clip != nullptr;
            cell["start"] = clip != nullptr &&
                            clip->start == static_cast<blokkily::Tick>(bar) * ticks_per_bar;
            cell["pattern"] = clip == nullptr ? -1 : static_cast<int>(clip->pattern);
            cell["repeats"] = clip == nullptr ? 0 : static_cast<int>(clip->repeats);
            cell["startBar"] = clip == nullptr
                                   ? -1
                                   : static_cast<int>(clip->start / ticks_per_bar);
            cell["name"] = clip == nullptr
                               ? QString()
                               : QString::fromStdString(song_.patterns[clip->pattern].name);
            lane.push_back(cell);
        }
        all.push_back(lane);
    }
    return all;
}

void SongModel::placeClip(int track, int bar) {
    if (!validTrack(track) || bar < 0 || clipAt(track, bar) != nullptr) return;
    checkpoint();
    song_.clips.push_back({static_cast<std::size_t>(track),
                           static_cast<std::size_t>(current_pattern_),
                           static_cast<blokkily::Tick>(bar) * ticks_per_bar, 1});
    notifyStructureChanged();
}

bool SongModel::removeClip(int track, int bar) {
    if (!validTrack(track) || bar < 0) return false;
    const auto* covering = clipAt(track, bar);
    if (covering == nullptr) return false;
    checkpoint();
    song_.clips.erase(song_.clips.begin() + (covering - song_.clips.data()));
    notifyStructureChanged();
    return true;
}

void SongModel::toggleClip(int track, int bar) {
    if (clipAt(track, bar) != nullptr) (void)removeClip(track, bar);
    else placeClip(track, bar);
}

int SongModel::openClip(int track, int bar) {
    const auto* covering = clipAt(track, bar);
    if (covering == nullptr) return -1;
    selectTrack(track);
    selectPattern(static_cast<int>(covering->pattern));
    return static_cast<int>(covering->pattern);
}

bool SongModel::setClipRepeats(int track, int bar, int repeats) {
    auto* covering = clipAt(track, bar);
    if (covering == nullptr) return false;
    const int next = qBound(1, repeats, 64);
    if (static_cast<int>(covering->repeats) == next) return true;
    // Extending must not land on another clip of this track.
    const auto start_bar = static_cast<int>(covering->start / ticks_per_bar);
    for (int probe = start_bar; probe < start_bar + next; ++probe) {
        const auto* other = static_cast<const SongModel*>(this)->clipAt(track, probe);
        if (other != nullptr && other != covering) return false;
    }
    checkpoint();
    covering->repeats = static_cast<std::uint32_t>(next);
    notifyStructureChanged();
    return true;
}

bool SongModel::moveClip(int track, int bar, int newBar) {
    auto* covering = clipAt(track, bar);
    if (covering == nullptr || newBar < 0) return false;
    const auto reps = static_cast<int>(std::max<std::uint32_t>(1, covering->repeats));
    const auto old_start = covering->start;
    if (old_start == static_cast<blokkily::Tick>(newBar) * ticks_per_bar) return true;
    // The destination span must be free of every other clip on this track.
    for (int probe = newBar; probe < newBar + reps; ++probe) {
        const auto* other = static_cast<const SongModel*>(this)->clipAt(track, probe);
        if (other != nullptr && other != covering) return false;
    }
    checkpoint();
    covering->start = static_cast<blokkily::Tick>(newBar) * ticks_per_bar;
    notifyStructureChanged();
    return true;
}
