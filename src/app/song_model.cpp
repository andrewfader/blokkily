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

void SongModel::addTrack() { addTrack(blokkily::InstrumentSlot{}); }

void SongModel::addTrack(const blokkily::InstrumentSlot& instrument) {
    checkpoint();
    // Numbered past the highest number in use, so deleting a track and adding
    // another never produces two of the same name.
    int number = static_cast<int>(song_.tracks.size()) + 1;
    const auto taken = [this](int candidate) {
        const auto name = QString("TRACK %1").arg(candidate).toStdString();
        return std::any_of(song_.tracks.begin(), song_.tracks.end(),
                           [&](const blokkily::Track& track) { return track.name == name; });
    };
    while (taken(number)) ++number;
    song_.tracks.push_back({QString("TRACK %1").arg(number).toStdString(), instrument, {}});
    peaks_.push_back(0.0F);
    selected_track_ = static_cast<int>(song_.tracks.size()) - 1;
    notifyStructureChanged();
}

void SongModel::addPattern() {
    checkpoint();
    song_.patterns.push_back({QString("PATTERN %1").arg(song_.patterns.size() + 1).toStdString(),
                              blokkily::Pattern(1920, 480)});
    current_pattern_ = static_cast<int>(song_.patterns.size()) - 1;
    notifyStructureChanged();
}

void SongModel::setTrackGain(int track, double decibels) {
    if (!validTrack(track)) return;
    checkpoint(QString("gain:%1").arg(track));
    song_.tracks[static_cast<std::size_t>(track)].mix.gain_db =
        qBound(blokkily::minimum_audible_db, decibels, 6.0);
    emit mixChanged();
    emit songChanged();
}

void SongModel::setTrackPan(int track, double pan) {
    if (!validTrack(track)) return;
    checkpoint(QString("pan:%1").arg(track));
    song_.tracks[static_cast<std::size_t>(track)].mix.pan = qBound(-1.0, pan, 1.0);
    emit mixChanged();
    emit songChanged();
}

void SongModel::toggleMute(int track) {
    if (!validTrack(track)) return;
    checkpoint();
    auto& mix = song_.tracks[static_cast<std::size_t>(track)].mix;
    mix.mute = !mix.mute;
    emit mixChanged();
    emit songChanged();
}

void SongModel::toggleSolo(int track) {
    if (!validTrack(track)) return;
    checkpoint();
    auto& mix = song_.tracks[static_cast<std::size_t>(track)].mix;
    mix.solo = !mix.solo;
    emit mixChanged();
    emit songChanged();
}

void SongModel::setMasterGain(double decibels) {
    checkpoint(QStringLiteral("master"));
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

void SongModel::replace(blokkily::Song song) {
    song_ = std::move(song);
    if (song_.patterns.empty()) song_.patterns.push_back({"PATTERN 1", blokkily::Pattern(1920, 480)});
    if (song_.tracks.empty()) song_.tracks.push_back({"TRACK 1", {}, {}});
    current_pattern_ = 0;
    selected_track_ = 0;
    peaks_.assign(song_.tracks.size(), 0.0F);
    clearHistory();
    emit tuningChanged();
    notifyStructureChanged();
}

void SongModel::setInstrument(int track, const blokkily::InstrumentSlot& slot) {
    if (!validTrack(track)) return;
    checkpoint();
    song_.tracks[static_cast<std::size_t>(track)].instrument = slot;
    notifyStructureChanged();
}

void SongModel::refreshStructure() { notifyStructureChanged(); }

QVariantList SongModel::meters() const {
    QVariantList fractions;
    for (std::size_t index = 0; index < song_.tracks.size(); ++index) {
        const double peak = index < peaks_.size() ? static_cast<double>(peaks_[index]) : 0.0;
        // A meter reads in decibels; the bar is that mapped onto the last 60 dB.
        fractions.push_back(peak <= 0.0 ? 0.0
                                         : qBound(0.0, (blokkily::linear_to_db(peak) + 60.0) / 60.0,
                                                  1.0));
    }
    return fractions;
}

void SongModel::setMeters(const std::vector<float>& track_peaks, float master_peak) {
    peaks_ = track_peaks;
    peaks_.resize(song_.tracks.size(), 0.0F);
    master_peak_ = master_peak;
    emit metersChanged();
}

QString SongModel::tuningName() const { return QString::fromStdString(song_.tuning.name); }

QStringList SongModel::tuningNames() const {
    QStringList names;
    for (const auto& tuning : blokkily::tuning_presets())
        names << QString::fromStdString(tuning.name);
    return names;
}

QString SongModel::scaleName() const { return QString::fromStdString(song_.scale.name); }

QStringList SongModel::scaleNames() const {
    QStringList names;
    for (const auto& scale : blokkily::scale_presets())
        names << QString::fromStdString(scale.name);
    return names;
}

QString SongModel::rootName() const { return degreeName(song_.root_degree); }

QString SongModel::degreeName(int degree) const {
    return QString::fromStdString(blokkily::degree_name(song_.tuning, degree));
}

// A note already in a pattern is stored as the pitch its instrument is told.
// Reading it back through the tuning is what lets one pattern be looked at in
// another tuning without any of its notes moving.
QString SongModel::pitchName(int key, double cents) const {
    return degreeName(blokkily::degree_for_pitch(
        song_.tuning, {static_cast<std::int16_t>(key), cents}));
}

blokkily::TunedPitch SongModel::pitchForDegree(int degree) const {
    return blokkily::degree_pitch(song_.tuning, degree);
}

int SongModel::snapDegree(int degree) const {
    if (!song_.auto_scale) return degree;
    return blokkily::snap_to_scale(song_.tuning, song_.scale, song_.root_degree, degree);
}

void SongModel::setTuning(const QString& name) {
    const auto tuning = blokkily::tuning_by_name(name.toStdString());
    if (!tuning || tuning->name == song_.tuning.name) return;
    checkpoint();
    // The root keeps its pitch class rather than its number: degree 60 is the
    // anchor in every tuning, so a root chosen as the fifth stays the fifth.
    const double from_root = blokkily::degree_cents(song_.tuning, song_.root_degree);
    song_.tuning = *tuning;
    song_.root_degree = blokkily::degree_for_pitch(
        song_.tuning, {static_cast<std::int16_t>(song_.tuning.anchor_key), from_root});
    emit tuningChanged();
    emit songChanged();
}

void SongModel::setScale(const QString& name) {
    const auto scale = blokkily::scale_by_name(name.toStdString());
    if (!scale || scale->name == song_.scale.name) return;
    checkpoint();
    song_.scale = *scale;
    emit tuningChanged();
    emit songChanged();
}

void SongModel::setRootDegree(int degree) {
    const int divisions = std::max(1, song_.tuning.divisions());
    // A root is a pitch class: it names the key the song is in, not an octave.
    const int anchored = song_.tuning.anchor_key +
                         ((degree - song_.tuning.anchor_key) % divisions + divisions) % divisions;
    if (anchored == song_.root_degree) return;
    checkpoint();
    song_.root_degree = anchored;
    emit tuningChanged();
    emit songChanged();
}

void SongModel::toggleAutoScale() {
    checkpoint();
    song_.auto_scale = !song_.auto_scale;
    emit tuningChanged();
    emit songChanged();
}

void SongModel::notifyStructureChanged() {
    emit structureChanged();
    emit songChanged();
}

namespace {
// How far back history reaches. Each step holds a whole song, which is small
// next to a minute of audio, so a generous limit costs little.
constexpr std::size_t history_limit = 200;
// Moves of one control closer together than this are one gesture.
constexpr qint64 merge_window_ms = 1200;

// A name as the interface shows it: trimmed, capitals, and not so long that it
// cannot fit a clip.
std::string display_name(const QString& name) {
    return name.trimmed().left(24).toUpper().toStdString();
}
} // namespace

SongModel::Snapshot SongModel::snapshot() const {
    return {song_, current_pattern_, selected_track_, state_id_};
}

void SongModel::restore(Snapshot snapshot) {
    song_ = std::move(snapshot.song);
    current_pattern_ = qBound(0, snapshot.current_pattern,
                              static_cast<int>(song_.patterns.size()) - 1);
    selected_track_ = qBound(0, snapshot.selected_track,
                             static_cast<int>(song_.tracks.size()) - 1);
    state_id_ = snapshot.id;
    peaks_.assign(song_.tracks.size(), 0.0F);
    // A step taken back cannot be merged with the next move of a control.
    last_merge_.clear();
    emit tuningChanged();
    emit mixChanged();
    notifyStructureChanged();
    emit historyChanged();
}

void SongModel::beginGesture() {
    gesture_open_ = true;
    gesture_recorded_ = false;
}

void SongModel::endGesture() {
    gesture_open_ = false;
    gesture_recorded_ = false;
}

void SongModel::checkpoint(const QString& merge) {
    if (gesture_open_) {
        if (gesture_recorded_) return;
        gesture_recorded_ = true;
    }
    if (!merge.isEmpty() && merge == last_merge_ && merge_clock_.isValid() &&
        merge_clock_.elapsed() < merge_window_ms) {
        merge_clock_.restart();
        return;
    }
    const bool was_dirty = dirty();
    const bool could_undo = canUndo();
    const bool could_redo = canRedo();
    undo_.push_back(snapshot());
    if (undo_.size() > history_limit) undo_.erase(undo_.begin());
    redo_.clear();
    state_id_ = next_id_++;
    last_merge_ = merge;
    merge_clock_.restart();
    if (was_dirty != dirty() || could_undo != canUndo() || could_redo != canRedo())
        emit historyChanged();
}

bool SongModel::undo() {
    if (undo_.empty()) return false;
    redo_.push_back(snapshot());
    auto previous = std::move(undo_.back());
    undo_.pop_back();
    restore(std::move(previous));
    return true;
}

bool SongModel::redo() {
    if (redo_.empty()) return false;
    undo_.push_back(snapshot());
    auto next = std::move(redo_.back());
    redo_.pop_back();
    restore(std::move(next));
    return true;
}

void SongModel::markSaved() {
    saved_id_ = state_id_;
    last_merge_.clear();
    emit historyChanged();
}

void SongModel::clearHistory() {
    undo_.clear();
    redo_.clear();
    state_id_ = next_id_++;
    last_merge_.clear();
    emit historyChanged();
}

void SongModel::duplicatePattern() {
    checkpoint();
    auto copy = song_.patterns[static_cast<std::size_t>(current_pattern_)];
    // "VERSE" becomes "VERSE 2", then "VERSE 3": a copy is named after what it
    // copies, and never collides with a name already in the song.
    const QString base = QString::fromStdString(copy.name);
    for (int suffix = 2;; ++suffix) {
        const auto candidate = QString("%1 %2").arg(base).arg(suffix).toStdString();
        const bool taken = std::any_of(song_.patterns.begin(), song_.patterns.end(),
            [&](const blokkily::PatternSlot& slot) { return slot.name == candidate; });
        if (!taken) { copy.name = candidate; break; }
    }
    song_.patterns.push_back(std::move(copy));
    current_pattern_ = static_cast<int>(song_.patterns.size()) - 1;
    notifyStructureChanged();
}

void SongModel::clearPattern() {
    auto& pattern = song_.patterns[static_cast<std::size_t>(current_pattern_)].pattern;
    if (pattern.events().empty()) return;
    checkpoint();
    pattern = blokkily::Pattern(pattern.length(), pattern.ticks_per_beat());
    notifyStructureChanged();
}

bool SongModel::deletePattern(int index) {
    // A song always has a pattern open, so the last one stays.
    if (index < 0 || static_cast<std::size_t>(index) >= song_.patterns.size() ||
        song_.patterns.size() < 2)
        return false;
    checkpoint();
    const auto removed = static_cast<std::size_t>(index);
    song_.patterns.erase(song_.patterns.begin() + index);
    // Its clips go with it, and every clip of a later pattern still names the
    // same pattern after the list closes up.
    std::erase_if(song_.clips, [removed](const blokkily::Clip& clip) {
        return clip.pattern == removed;
    });
    for (auto& clip : song_.clips)
        if (clip.pattern > removed) --clip.pattern;
    if (current_pattern_ >= index && current_pattern_ > 0) --current_pattern_;
    notifyStructureChanged();
    return true;
}

bool SongModel::deleteTrack(int track) {
    if (!validTrack(track) || song_.tracks.size() < 2) return false;
    checkpoint();
    const auto removed = static_cast<std::size_t>(track);
    song_.tracks.erase(song_.tracks.begin() + track);
    std::erase_if(song_.clips, [removed](const blokkily::Clip& clip) {
        return clip.track == removed;
    });
    for (auto& clip : song_.clips)
        if (clip.track > removed) --clip.track;
    if (removed < peaks_.size()) peaks_.erase(peaks_.begin() + track);
    if (selected_track_ >= track && selected_track_ > 0) --selected_track_;
    notifyStructureChanged();
    return true;
}

void SongModel::renamePattern(int index, const QString& name) {
    if (index < 0 || static_cast<std::size_t>(index) >= song_.patterns.size()) return;
    const auto wanted = display_name(name);
    auto& slot = song_.patterns[static_cast<std::size_t>(index)];
    if (wanted.empty() || wanted == slot.name) return;
    checkpoint();
    slot.name = wanted;
    emit songChanged();
}

void SongModel::renameTrack(int track, const QString& name) {
    if (!validTrack(track)) return;
    const auto wanted = display_name(name);
    auto& target = song_.tracks[static_cast<std::size_t>(track)];
    if (wanted.empty() || wanted == target.name) return;
    checkpoint();
    target.name = wanted;
    emit songChanged();
}
