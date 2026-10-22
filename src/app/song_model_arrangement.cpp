#include "song_model.hpp"

#include "blokkily/audio/comping.hpp"

#include "blokkily/audio/mixer.hpp"

#include <QFileInfo>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

const blokkily::Clip* SongModel::clipAt(int track, int bar) const {
    if (!validTrack(track) || bar < 0) return nullptr;
    const auto tick = barStart(bar);
    const auto covers = [&](const blokkily::Clip& clip) {
        if (clip.track != static_cast<std::size_t>(track)) return false;
        if (clip.pattern >= song_.patterns.size()) return false;
        const auto span = clip.span(song_.patterns[clip.pattern].pattern.length());
        return tick >= clip.start && tick < clip.start + span;
    };
    // A take playing there is the bar's clip, over what it was recorded on
    // (features/comping_and_punch.feature); otherwise the first in the song.
    for (std::size_t index = 0; index < song_.clips.size(); ++index)
        if (covers(song_.clips[index]) && !song_.clips[index].comped_out &&
            blokkily::is_midi_take(song_, index))
            return &song_.clips[index];
    const auto found = std::find_if(song_.clips.begin(), song_.clips.end(), covers);
    return found == song_.clips.end() ? nullptr : &*found;
}

blokkily::Clip* SongModel::clipAt(int track, int bar) {
    return const_cast<blokkily::Clip*>(
        static_cast<const SongModel*>(this)->clipAt(track, bar));
}

bool SongModel::hasClip(int track, int bar) const { return clipAt(track, bar) != nullptr; }

QVariantList SongModel::lanes() const {
    if (lanes_cache_) return *lanes_cache_;
    QVariantList all;
    const int bar_count = bars();
    // Each bar's clip, found by walking every clip once over the bars it
    // covers; the first clip in the song's order wins, as in clipAt.
    std::vector<std::vector<const blokkily::Clip*>> at(
        static_cast<std::size_t>(std::max(0, trackCount())),
        std::vector<const blokkily::Clip*>(static_cast<std::size_t>(std::max(0, bar_count)), nullptr));
    // The takes playing go first, so a bar shows the take over what it was
    // recorded on; then the song's order, first clip winning.
    std::vector<const blokkily::Clip*> order;
    for (std::size_t index = 0; index < song_.clips.size(); ++index)
        if (!song_.clips[index].comped_out && blokkily::is_midi_take(song_, index))
            order.push_back(&song_.clips[index]);
    for (const auto& clip : song_.clips) order.push_back(&clip);
    for (const auto* placed : order) {
        const auto& clip = *placed;
        if (clip.track >= at.size() || clip.pattern >= song_.patterns.size()) continue;
        const auto span = clip.span(song_.patterns[clip.pattern].pattern.length());
        if (span <= 0) continue;
        // A bar is the clip's when the bar starts inside it.
        int first = barAt(clip.start);
        if (barStart(first) < clip.start) ++first;
        const int last = std::min(bar_count - 1, barAt(clip.start + span - 1));
        auto& lane = at[clip.track];
        for (int bar = std::max(0, first); bar <= last; ++bar)
            if (lane[static_cast<std::size_t>(bar)] == nullptr) lane[static_cast<std::size_t>(bar)] = &clip;
    }
    for (int track = 0; track < trackCount(); ++track) {
        QVariantList lane;
        for (int bar = 0; bar < bar_count; ++bar) {
            const auto* clip = at[static_cast<std::size_t>(track)][static_cast<std::size_t>(bar)];
            QVariantMap cell;
            cell["filled"] = clip != nullptr;
            // A part with nothing in it yet: drawn quietly, still a clip that
            // plays whatever is written into it.
            cell["empty"] = clip != nullptr &&
                            song_.patterns[clip->pattern].pattern.events().empty() &&
                            song_.patterns[clip->pattern].pattern.continuous().empty();
            cell["start"] = clip != nullptr && clip->start == barStart(bar);
            cell["pattern"] = clip == nullptr ? -1 : static_cast<int>(clip->pattern);
            cell["repeats"] = clip == nullptr ? 0 : static_cast<int>(clip->repeats);
            cell["startBar"] = clip == nullptr ? -1 : barAt(clip->start);
            cell["name"] = clip == nullptr
                               ? QString()
                               : QString::fromStdString(song_.patterns[clip->pattern].name);
            // A take: its place in its stack, for the TAKE badge.
            const auto index = clip == nullptr ? song_.clips.size()
                                               : static_cast<std::size_t>(clip - song_.clips.data());
            const bool take = clip != nullptr && blokkily::is_midi_take(song_, index);
            cell["clip"] = clip == nullptr ? -1 : static_cast<int>(index);
            cell["take"] = take;
            if (take) {
                const auto stack = blokkily::midi_take_stack(song_, index);
                const auto at_stack = std::find(stack.begin(), stack.end(), index);
                cell["takeIndex"] = static_cast<int>(at_stack - stack.begin()) + 1;
                cell["takeCount"] = static_cast<int>(stack.size());
            }
            lane.push_back(cell);
        }
        all.push_back(lane);
    }
    lanes_cache_ = all;
    return all;
}

void SongModel::placeClip(int track, int bar) {
    if (!validTrack(track) || bar < 0 || clipAt(track, bar) != nullptr) return;
    const int part = partOf(current_section_, track);
    if (part < 0) return;
    checkpoint();
    song_.clips.push_back({static_cast<std::size_t>(track), static_cast<std::size_t>(part),
                           barStart(bar), 1});
    notifyStructureChanged();
}

void SongModel::placeSection(int bar) {
    if (bar < 0) return;
    // Nothing to do, and no step of history, when every track is busy there.
    auto trial = song_;
    if (trial.place_section(static_cast<std::size_t>(current_section_), barStart(bar)) == 0)
        return;
    checkpoint();
    song_ = std::move(trial);
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
    const int part = static_cast<int>(covering->pattern);
    selectTrack(track);
    selectSection(static_cast<int>(song_.patterns[static_cast<std::size_t>(part)].section));
    return part;
}

bool SongModel::setClipRepeats(int track, int bar, int repeats) {
    auto* covering = clipAt(track, bar);
    if (covering == nullptr) return false;
    const int next = qBound(1, repeats, 64);
    if (static_cast<int>(covering->repeats) == next) return true;
    // Extending must not land on another clip of this track.
    const auto length = song_.patterns[covering->pattern].pattern.length();
    const int start_bar = barAt(covering->start);
    const int end_bar = barAt(covering->start + length * next - 1);
    for (int probe = start_bar; probe <= end_bar; ++probe) {
        const auto* other = static_cast<const SongModel*>(this)->clipAt(track, probe);
        if (other != nullptr && other != covering) return false;
    }
    checkpoint();
    covering->repeats = static_cast<std::uint32_t>(next);
    // Repeats set by hand are whole: a launcher take's cut goes with them.
    covering->length = 0;
    notifyStructureChanged();
    return true;
}

bool SongModel::extendClipToBar(int track, int startBar, int bar) {
    const auto* covering = static_cast<const SongModel*>(this)->clipAt(track, startBar);
    if (covering == nullptr || bar < startBar) return false;
    const auto length = song_.patterns[covering->pattern].pattern.length();
    if (length <= 0) return false;
    // The clip ends with the bar: as many whole patterns as reach its end.
    const auto end = song_.meter.bar_start(bar + 1);
    const auto wanted = std::max<blokkily::Tick>(1, (end - covering->start + length - 1) / length);
    return setClipRepeats(track, startBar, static_cast<int>(std::min<blokkily::Tick>(wanted, 64)));
}

bool SongModel::spanFree(int track, blokkily::Tick start, blokkily::Tick span,
                         const blokkily::Clip* ignore) const {
    for (const auto& clip : song_.clips) {
        if (&clip == ignore || clip.track != static_cast<std::size_t>(track) || clip.comped_out ||
            clip.pattern >= song_.patterns.size())
            continue;
        const auto other = clip.span(song_.patterns[clip.pattern].pattern.length());
        if (clip.start < start + span && start < clip.start + other) return false;
    }
    return true;
}

bool SongModel::copyClip(int track, int bar, int newBar) {
    const auto* covering = clipAt(track, bar);
    if (covering == nullptr || newBar < 0) return false;
    const auto span = covering->span(song_.patterns[covering->pattern].pattern.length());
    const auto start = barStart(newBar);
    if (!spanFree(track, start, span)) return false;
    auto copy = *covering;
    copy.start = start;
    copy.comped_out = false;
    checkpoint();
    song_.clips.push_back(copy);
    notifyStructureChanged();
    return true;
}

bool SongModel::copyClipToClipboard(int track, int bar) {
    const auto* covering = clipAt(track, bar);
    if (covering == nullptr) return false;
    clip_clipboard_ = *covering;
    clip_clipboard_->comped_out = false;
    clip_clipboard_section_.reset();
    if (!song_.sections.empty())
        clip_clipboard_section_ = song_.patterns[covering->pattern].section;
    emit clipboardChanged();
    return true;
}

bool SongModel::pasteClip(int track, int bar) {
    if (!clip_clipboard_ || !validTrack(track) || bar < 0) return false;
    auto copy = *clip_clipboard_;
    if (clip_clipboard_section_) {
        // The pasting track's own part of the copied pattern.
        const auto part = song_.part(*clip_clipboard_section_, static_cast<std::size_t>(track));
        if (!part) return false;
        copy.pattern = *part;
    } else if (copy.track != static_cast<std::size_t>(track) || copy.pattern >= song_.patterns.size()) {
        return false;
    }
    copy.track = static_cast<std::size_t>(track);
    copy.start = barStart(bar);
    const auto span = copy.span(song_.patterns[copy.pattern].pattern.length());
    if (!spanFree(track, copy.start, span)) return false;
    checkpoint();
    song_.clips.push_back(copy);
    notifyStructureChanged();
    return true;
}

bool SongModel::moveClip(int track, int bar, int newBar) {
    auto* covering = clipAt(track, bar);
    if (covering == nullptr || newBar < 0) return false;
    const auto span = covering->span(song_.patterns[covering->pattern].pattern.length());
    const auto old_start = covering->start;
    const auto new_start = barStart(newBar);
    if (old_start == new_start) return true;
    // The destination span must be free of every other clip on this track.
    for (int probe = newBar; probe <= barAt(new_start + span - 1); ++probe) {
        const auto* other = static_cast<const SongModel*>(this)->clipAt(track, probe);
        if (other != nullptr && other != covering) return false;
    }
    checkpoint();
    covering->start = new_start;
    notifyStructureChanged();
    return true;
}
