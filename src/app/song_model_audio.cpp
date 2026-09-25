// Audio clips as the interface edits them (item 2.2). Every edit names its
// clip by AudioClip::id, so a drag keeps hold of its clip while others come
// and go, and every edit is a change to the one song: the engine hears it
// through the same structureChanged recompile a step edit takes.

#include "song_model.hpp"

#include "blokkily/audio/audio_clips.hpp"

#include <QFileInfo>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

bool tick_in_range(double tick) {
    return std::isfinite(tick) &&
           tick < static_cast<double>(std::numeric_limits<std::int32_t>::max());
}

std::uint64_t whole_frames(double frames) {
    return frames <= 0.0 ? 0 : static_cast<std::uint64_t>(std::llround(frames));
}

// Fades never add up to more than the clip: a shortened clip shortens its
// fades, the fade-out first.
void fit_fades(blokkily::AudioClip& clip) {
    clip.fade_in_frames = std::min(clip.fade_in_frames, clip.length_frames);
    clip.fade_out_frames = std::min(clip.fade_out_frames,
                                    clip.length_frames - clip.fade_in_frames);
}

} // namespace

blokkily::AudioClip* SongModel::findAudioClip(qint64 id) {
    for (auto& clip : song_.audio_clips)
        if (static_cast<qint64>(clip.id) == id) return &clip;
    return nullptr;
}

const blokkily::AudioClip* SongModel::findAudioClip(qint64 id) const {
    for (const auto& clip : song_.audio_clips)
        if (static_cast<qint64>(clip.id) == id) return &clip;
    return nullptr;
}

QVariantMap SongModel::audioClipRow(const blokkily::AudioClip& clip) const {
    QVariantMap row;
    const auto& file = song_.audio_files.at(clip.file);
    const double start = static_cast<double>(clip.start);
    const double end = blokkily::audio_clip_end_tick(song_, clip);
    const double seconds = static_cast<double>(clip.length_frames) / std::max(1U, file.sample_rate);
    // Where the fades meet the clip, in ticks: the lane draws them there.
    const auto tick_after = [&](std::uint64_t frames) {
        const auto resolution = song_.ticks_per_beat();
        return song_.tempo.tick_at_seconds(
            song_.tempo.seconds_at(clip.start, resolution) +
                static_cast<double>(frames) / std::max(1U, file.sample_rate),
            resolution);
    };
    row["id"] = static_cast<qint64>(clip.id);
    row["track"] = static_cast<int>(clip.track);
    row["file"] = static_cast<int>(clip.file);
    row["name"] = QFileInfo(QString::fromStdString(file.path.string())).completeBaseName();
    row["path"] = QString::fromStdString(file.path.string());
    row["startTick"] = start;
    row["endTick"] = end;
    row["fadeInTick"] = tick_after(clip.fade_in_frames);
    row["fadeOutTick"] = tick_after(clip.length_frames - clip.fade_out_frames);
    row["gainDb"] = clip.gain_db;
    row["lengthSeconds"] = seconds;
    row["offsetFrames"] = static_cast<qint64>(clip.offset_frames);
    row["lengthFrames"] = static_cast<qint64>(clip.length_frames);
    row["fadeInFrames"] = static_cast<qint64>(clip.fade_in_frames);
    row["fadeOutFrames"] = static_cast<qint64>(clip.fade_out_frames);
    row["missing"] = clip.file < missing_audio_.size() && missing_audio_[clip.file];
    return row;
}

QVariantList SongModel::audioClips() const {
    QVariantList rows;
    for (const auto& clip : song_.audio_clips) {
        if (clip.file >= song_.audio_files.size() || clip.track >= song_.tracks.size()) continue;
        rows.push_back(audioClipRow(clip));
    }
    return rows;
}

int SongModel::missingAudioFiles() const {
    int missing = 0;
    for (std::size_t index = 0; index < song_.audio_files.size(); ++index)
        if (index < missing_audio_.size() && missing_audio_[index]) ++missing;
    return missing;
}

QVariantMap SongModel::audioClip(qint64 id) const {
    const auto* clip = findAudioClip(id);
    if (clip == nullptr || clip->file >= song_.audio_files.size()) return {};
    return audioClipRow(*clip);
}

void SongModel::setMissingAudio(std::vector<bool> missing) {
    if (missing == missing_audio_) return;
    missing_audio_ = std::move(missing);
    emit audioClipsChanged();
}

int SongModel::addAudioTrack() {
    checkpoint();
    int number = 1;
    const auto taken = [this](int candidate) {
        const auto name = QString("AUDIO %1").arg(candidate).toStdString();
        return std::any_of(song_.tracks.begin(), song_.tracks.end(),
                           [&](const blokkily::Track& track) { return track.name == name; });
    };
    while (taken(number)) ++number;
    blokkily::Track track;
    track.name = QString("AUDIO %1").arg(number).toStdString();
    song_.tracks.push_back(std::move(track));
    peaks_.push_back(0.0F);
    selected_track_ = static_cast<int>(song_.tracks.size()) - 1;
    notifyStructureChanged();
    return selected_track_;
}

blokkily::AudioClipId SongModel::addAudioClip(const blokkily::AudioFileRef& file, int track,
                                              blokkily::Tick start) {
    if (!validTrack(track) || start < 0 || file.frames == 0 || file.sample_rate == 0 ||
        file.channels == 0 || file.path.empty())
        return 0;
    checkpoint();
    blokkily::AudioClip clip;
    clip.id = song_.next_audio_clip_id();
    clip.track = static_cast<std::size_t>(track);
    clip.file = blokkily::add_audio_file(song_, file);
    clip.start = start;
    clip.length_frames = file.frames;
    song_.audio_clips.push_back(clip);
    notifyStructureChanged();
    return clip.id;
}

bool SongModel::moveAudioClip(qint64 id, double tick, int track) {
    auto* clip = findAudioClip(id);
    if (clip == nullptr || !validTrack(track) || !tick_in_range(tick)) return false;
    const auto start = static_cast<blokkily::Tick>(std::llround(std::max(0.0, tick)));
    if (start == clip->start && static_cast<std::size_t>(track) == clip->track) return true;
    checkpoint();
    clip = findAudioClip(id);
    clip->start = start;
    clip->track = static_cast<std::size_t>(track);
    notifyStructureChanged();
    return true;
}

bool SongModel::trimAudioClipStart(qint64 id, double tick) {
    auto* clip = findAudioClip(id);
    if (clip == nullptr || !tick_in_range(tick)) return false;
    const auto& file = song_.audio_files.at(clip->file);
    const double wanted = std::max(0.0, tick);
    // Frames the edge moves by: into the clip (positive) hides the file's
    // start, out of it reveals what lies before.
    const double moved = blokkily::frames_between(song_, static_cast<double>(clip->start),
                                                  wanted, file.sample_rate);
    auto delta = static_cast<std::int64_t>(std::llround(moved));
    const auto earliest = -static_cast<std::int64_t>(clip->offset_frames);
    const auto latest = static_cast<std::int64_t>(clip->length_frames) - 1;
    delta = std::clamp(delta, earliest, latest);
    if (delta == 0) return true;
    // The new start is the tick where the revealed or hidden frame sounds,
    // so the rest of the clip stays exactly where it was.
    const auto resolution = song_.ticks_per_beat();
    const double seconds = song_.tempo.seconds_at(clip->start, resolution) +
                           static_cast<double>(delta) / file.sample_rate;
    const auto start = static_cast<blokkily::Tick>(
        std::llround(std::max(0.0, song_.tempo.tick_at_seconds(seconds, resolution))));
    checkpoint();
    clip = findAudioClip(id);
    clip->start = start;
    clip->offset_frames = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(clip->offset_frames) + delta);
    clip->length_frames = static_cast<std::uint64_t>(
        static_cast<std::int64_t>(clip->length_frames) - delta);
    fit_fades(*clip);
    notifyStructureChanged();
    return true;
}

bool SongModel::trimAudioClipEnd(qint64 id, double tick) {
    auto* clip = findAudioClip(id);
    if (clip == nullptr || !tick_in_range(tick)) return false;
    const auto& file = song_.audio_files.at(clip->file);
    const double frames = blokkily::frames_between(song_, static_cast<double>(clip->start), tick,
                                                   file.sample_rate);
    const std::uint64_t available = file.frames - clip->offset_frames;
    const auto length = std::clamp<std::uint64_t>(whole_frames(frames), 1, available);
    if (length == clip->length_frames) return true;
    checkpoint();
    clip = findAudioClip(id);
    clip->length_frames = length;
    fit_fades(*clip);
    notifyStructureChanged();
    return true;
}

bool SongModel::setAudioClipFadeIn(qint64 id, double tick) {
    auto* clip = findAudioClip(id);
    if (clip == nullptr || !tick_in_range(tick)) return false;
    const auto rate = song_.audio_files.at(clip->file).sample_rate;
    const auto frames = std::min(
        whole_frames(blokkily::frames_between(song_, static_cast<double>(clip->start), tick, rate)),
        clip->length_frames - clip->fade_out_frames);
    if (frames == clip->fade_in_frames) return true;
    checkpoint();
    clip = findAudioClip(id);
    clip->fade_in_frames = frames;
    notifyStructureChanged();
    return true;
}

bool SongModel::setAudioClipFadeOut(qint64 id, double tick) {
    auto* clip = findAudioClip(id);
    if (clip == nullptr || !tick_in_range(tick)) return false;
    const auto rate = song_.audio_files.at(clip->file).sample_rate;
    const double into = blokkily::frames_between(song_, static_cast<double>(clip->start), tick, rate);
    const double remaining = static_cast<double>(clip->length_frames) - std::max(0.0, into);
    const auto frames = std::min(whole_frames(remaining),
                                 clip->length_frames - clip->fade_in_frames);
    if (frames == clip->fade_out_frames) return true;
    checkpoint();
    clip = findAudioClip(id);
    clip->fade_out_frames = frames;
    notifyStructureChanged();
    return true;
}

bool SongModel::setAudioClipGain(qint64 id, double decibels) {
    auto* clip = findAudioClip(id);
    if (clip == nullptr || !std::isfinite(decibels)) return false;
    const double gain = std::clamp(decibels, -60.0, 12.0);
    if (gain == clip->gain_db) return true;
    // A wheel turned over a clip sends a stream of these: one step of history.
    checkpoint(QString("audio-gain-%1").arg(id));
    clip = findAudioClip(id);
    clip->gain_db = gain;
    notifyStructureChanged();
    return true;
}

bool SongModel::removeAudioClip(qint64 id) {
    if (findAudioClip(id) == nullptr) return false;
    checkpoint();
    // The file stays listed, so an undo brings the clip back with it; files
    // nothing plays are dropped from what is saved (Song::prune_audio_files).
    std::erase_if(song_.audio_clips, [id](const blokkily::AudioClip& clip) {
        return static_cast<qint64>(clip.id) == id;
    });
    notifyStructureChanged();
    return true;
}
