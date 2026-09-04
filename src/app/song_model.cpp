#include "song_model.hpp"

#include "blokkily/audio/mixer.hpp"

#include <QFileInfo>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace {

// What a track's strip should call its instrument. An empty slot says so
// rather than showing a blank, because an empty track is a normal state.
QString instrument_label(const blokkily::InstrumentSlot& slot) {
    if (slot.format.empty()) return QStringLiteral("no instrument");
    const QString file = QFileInfo(QString::fromStdString(slot.path)).completeBaseName();
    const QString format = QString::fromStdString(slot.format) == "SoundFont"
                               ? QStringLiteral("SF")
                               : QString::fromStdString(slot.format);
    return file.isEmpty() ? format : QString("%1 · %2").arg(format, file);
}

QString gain_readout(double decibels) {
    if (decibels <= blokkily::minimum_audible_db) return QStringLiteral("-inf");
    return QString("%1%2").arg(decibels > 0.0 ? "+" : "").arg(decibels, 0, 'f', 1);
}

QString pan_readout(double pan) {
    if (std::abs(pan) < 0.01) return QStringLiteral("C");
    return QString("%1%2").arg(pan < 0 ? "L" : "R").arg(qRound(std::abs(pan) * 100.0));
}

} // namespace

SongModel::SongModel(QObject* parent) : QObject(parent) {
    song_.patterns = {{"VERSE", blokkily::Pattern(1920, 480)},
                      {"CHORUS", blokkily::Pattern(1920, 480)}};
    song_.tracks = {{"DRUMS", {}, {}}, {"BASS", {}, {-3.0, 0.25, false, false}}};
    // A short arrangement out of the box, so the timeline is something to edit
    // rather than an empty grid that has to be explained.
    song_.clips = {{0, 0, 0, 2}, {0, 1, 2 * ticks_per_bar, 2}, {1, 1, 2 * ticks_per_bar, 2}};
    peaks_.assign(song_.tracks.size(), 0.0F);

    // A four-to-the-floor verse with one parameter-locked accent, and a chorus
    // that answers it, so both the editors and the timeline open with music.
    for (const auto [step, key] : {std::pair{0, 36}, {4, 36}, {8, 38}, {12, 36}}) {
        blokkily::Trigger trigger;
        trigger.start = step * 120;
        trigger.duration = 96;
        trigger.musical_data = blokkily::Note{static_cast<std::int16_t>(key), 0.9F, 0.0F};
        if (step == 8)
            trigger.locks = {{"level", 0, 0.75, blokkily::ParameterLock::Kind::automation}};
        (void)song_.patterns[0].pattern.add(trigger);
    }
    for (const auto [step, key] : {std::pair{0, 43}, {6, 46}, {10, 48}}) {
        blokkily::Trigger trigger;
        trigger.start = step * 120;
        trigger.duration = 120;
        trigger.musical_data = blokkily::Note{static_cast<std::int16_t>(key), 0.8F, 0.0F};
        (void)song_.patterns[1].pattern.add(trigger);
    }
}

bool SongModel::validTrack(int track) const {
    return track >= 0 && static_cast<std::size_t>(track) < song_.tracks.size();
}

blokkily::Pattern& SongModel::editPattern() {
    return song_.patterns[static_cast<std::size_t>(current_pattern_)].pattern;
}

const blokkily::Pattern& SongModel::editPattern() const {
    return song_.patterns[static_cast<std::size_t>(current_pattern_)].pattern;
}

int SongModel::bars() const {
    const auto used = static_cast<int>((song_.length() + ticks_per_bar - 1) / ticks_per_bar);
    // Always show a spare bar past the end so the song can be extended by
    // clicking, and never fewer than a readable two-phrase window.
    return std::max(minimum_visible_bars, used + 1);
}

QVariantList SongModel::tracks() const {
    QVariantList rows;
    const bool solo = song_.any_solo();
    for (std::size_t index = 0; index < song_.tracks.size(); ++index) {
        const auto& track = song_.tracks[index];
        const auto gain = blokkily::strip_gain(track.mix, solo);
        QVariantMap row;
        row["index"] = static_cast<int>(index);
        row["name"] = QString::fromStdString(track.name);
        row["instrument"] = instrument_label(track.instrument);
        row["hasInstrument"] = !track.instrument.format.empty();
        row["gainDb"] = track.mix.gain_db;
        row["gainText"] = gain_readout(track.mix.gain_db);
        row["pan"] = track.mix.pan;
        row["panText"] = pan_readout(track.mix.pan);
        row["mute"] = track.mix.mute;
        row["solo"] = track.mix.solo;
        row["audible"] = blokkily::audible(track.mix, solo);
        row["selected"] = static_cast<int>(index) == selected_track_;
        row["peak"] = index < peaks_.size() ? static_cast<double>(peaks_[index]) : 0.0;
        // A meter reads in decibels; the bar is that mapped onto the last 60 dB.
        const double peak_db = blokkily::linear_to_db(row["peak"].toDouble());
        row["peakFraction"] = qBound(0.0, (peak_db + 60.0) / 60.0, 1.0);
        row["gainFraction"] = qBound(0.0, (track.mix.gain_db + 60.0) / 66.0, 1.0);
        row["silent"] = gain.silent();
        rows.push_back(row);
    }
    return rows;
}

QVariantList SongModel::patterns() const {
    QVariantList rows;
    for (std::size_t index = 0; index < song_.patterns.size(); ++index) {
        QVariantMap row;
        row["index"] = static_cast<int>(index);
        row["name"] = QString::fromStdString(song_.patterns[index].name);
        row["events"] = static_cast<int>(song_.patterns[index].pattern.events().size());
        row["current"] = static_cast<int>(index) == current_pattern_;
        rows.push_back(row);
    }
    return rows;
}

QVariantList SongModel::clips() const {
    QVariantList rows;
    for (const auto& clip : song_.clips) {
        if (clip.pattern >= song_.patterns.size()) continue;
        QVariantMap row;
        row["track"] = static_cast<int>(clip.track);
        row["pattern"] = static_cast<int>(clip.pattern);
        row["name"] = QString::fromStdString(song_.patterns[clip.pattern].name);
        row["bar"] = static_cast<int>(clip.start / ticks_per_bar);
        const auto span = song_.patterns[clip.pattern].pattern.length() *
                          static_cast<blokkily::Tick>(std::max<std::uint32_t>(1, clip.repeats));
        row["bars"] = std::max(1, static_cast<int>(span / ticks_per_bar));
        rows.push_back(row);
    }
    return rows;
}

void SongModel::selectTrack(int track) {
    if (!validTrack(track) || track == selected_track_) return;
    selected_track_ = track;
    emit songChanged();
}

void SongModel::selectPattern(int pattern) {
    if (pattern < 0 || static_cast<std::size_t>(pattern) >= song_.patterns.size()) return;
    if (pattern == current_pattern_) return;
    current_pattern_ = pattern;
    emit songChanged();
}

void SongModel::addTrack() {
    song_.tracks.push_back({QString("TRACK %1").arg(song_.tracks.size() + 1).toStdString(),
                            {}, {}});
    peaks_.push_back(0.0F);
    selected_track_ = static_cast<int>(song_.tracks.size()) - 1;
    notifyStructureChanged();
}

void SongModel::addPattern() {
    song_.patterns.push_back({QString("PATTERN %1").arg(song_.patterns.size() + 1).toStdString(),
                              blokkily::Pattern(1920, 480)});
    current_pattern_ = static_cast<int>(song_.patterns.size()) - 1;
    notifyStructureChanged();
}

void SongModel::setTrackGain(int track, double decibels) {
    if (!validTrack(track)) return;
    song_.tracks[static_cast<std::size_t>(track)].mix.gain_db =
        qBound(blokkily::minimum_audible_db, decibels, 6.0);
    emit mixChanged();
    emit songChanged();
}

void SongModel::setTrackPan(int track, double pan) {
    if (!validTrack(track)) return;
    song_.tracks[static_cast<std::size_t>(track)].mix.pan = qBound(-1.0, pan, 1.0);
    emit mixChanged();
    emit songChanged();
}

void SongModel::toggleMute(int track) {
    if (!validTrack(track)) return;
    auto& mix = song_.tracks[static_cast<std::size_t>(track)].mix;
    mix.mute = !mix.mute;
    emit mixChanged();
    emit songChanged();
}

void SongModel::toggleSolo(int track) {
    if (!validTrack(track)) return;
    auto& mix = song_.tracks[static_cast<std::size_t>(track)].mix;
    mix.solo = !mix.solo;
    emit mixChanged();
    emit songChanged();
}

void SongModel::setMasterGain(double decibels) {
    song_.master_gain_db = qBound(blokkily::minimum_audible_db, decibels, 6.0);
    emit mixChanged();
    emit songChanged();
}

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
            cell["name"] = clip == nullptr
                               ? QString()
                               : QString::fromStdString(song_.patterns[clip->pattern].name);
            lane.push_back(cell);
        }
        all.push_back(lane);
    }
    return all;
}

void SongModel::toggleClip(int track, int bar) {
    if (!validTrack(track) || bar < 0) return;
    if (const auto* covering = clipAt(track, bar)) {
        song_.clips.erase(song_.clips.begin() + (covering - song_.clips.data()));
    } else {
        song_.clips.push_back({static_cast<std::size_t>(track),
                               static_cast<std::size_t>(current_pattern_),
                               static_cast<blokkily::Tick>(bar) * ticks_per_bar, 1});
    }
    notifyStructureChanged();
}

void SongModel::replace(blokkily::Song song) {
    song_ = std::move(song);
    if (song_.patterns.empty()) song_.patterns.push_back({"PATTERN 1", blokkily::Pattern(1920, 480)});
    if (song_.tracks.empty()) song_.tracks.push_back({"TRACK 1", {}, {}});
    current_pattern_ = 0;
    selected_track_ = 0;
    peaks_.assign(song_.tracks.size(), 0.0F);
    notifyStructureChanged();
}

void SongModel::setInstrument(int track, const blokkily::InstrumentSlot& slot) {
    if (!validTrack(track)) return;
    song_.tracks[static_cast<std::size_t>(track)].instrument = slot;
    notifyStructureChanged();
}

void SongModel::setMeters(const std::vector<float>& track_peaks, float master_peak) {
    peaks_ = track_peaks;
    peaks_.resize(song_.tracks.size(), 0.0F);
    master_peak_ = master_peak;
    emit metersChanged();
}

void SongModel::notifyStructureChanged() {
    emit structureChanged();
    emit songChanged();
}
