#include "song_model.hpp"

#include "blokkily/audio/audio_input.hpp"
#include "blokkily/audio/event_queue.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/instruments/sampler_program.hpp"

#include <QFileInfo>
#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace {

// What a track's strip should call its instrument. An empty slot says so
// rather than showing a blank, because an empty track is a normal state.
QString instrument_label(const blokkily::InstrumentSlot& slot) {
    if (slot.format.empty()) return QStringLiteral("no instrument");
    // The built-in sampler has no file of its own: it is named by what it
    // plays, the first sample of its program, or by its mode while empty.
    if (slot.format == "Sampler") {
        const auto program = blokkily::parse_sampler(slot.state);
        const bool kit = program ? program->mode == blokkily::SamplerProgram::Mode::kit
                                 : slot.identifier == "kit";
        const QString sample = program && !program->zones.empty()
            ? QFileInfo(QString::fromStdString(program->zones.front().sample)).completeBaseName()
            : QString();
        return QString("SMP · %1").arg(sample.isEmpty() ? (kit ? "KIT" : "KEYS") : sample);
    }
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

// Pan position, formatted consistently. Center is "C 0", full-left is
// "L100", small offsets show one digit ("R25") so the readout stays
// aligned with the other mixer labels instead of dropping the number at
// center and breaking the scan pattern.
QString pan_readout(double pan) {
    const int pct = static_cast<int>(std::lround(std::abs(pan) * 100.0));
    if (pct == 0) return QStringLiteral("C 0");
    return QString("%1%2").arg(pan < 0 ? "L" : "R").arg(pct);
}

} // namespace

SongModel::SongModel(QObject* parent) : QObject(parent) {
    song_.patterns = {{"VERSE", blokkily::Pattern(1920, 480)},
                      {"CHORUS", blokkily::Pattern(1920, 480)}};
    song_.tracks = {{"DRUMS", {}, {}}, {"BASS", {}, {-3.0, 0.25, false, false}}};
    // A short arrangement out of the box, so the timeline is something to edit
    // rather than an empty grid that has to be explained.
    const auto bar_two = song_.meter.bar_start(2);
    song_.clips = {{0, 0, 0, 2}, {0, 1, bar_two, 2}, {1, 1, bar_two, 2}};
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
    const auto length = song_.length();
    // Bars the song reaches into, however long each one is.
    const int used = length > 0 ? barAt(length - 1) + 1 : 0;
    // Always show a spare bar past the end so the song can be extended by
    // clicking, and never fewer than a readable two-phrase window.
    return std::max(minimum_visible_bars, used + 1);
}

QVariantList SongModel::tracks() const {
    QVariantList rows;
    const bool solo = song_.any_solo();
    for (std::size_t index = 0; index < song_.tracks.size(); ++index) {
        const auto& track = song_.tracks[index];
        const auto gain = blokkily::strip_gain(song_.heard_strip(index), solo);
        QVariantMap row;
        row["index"] = static_cast<int>(index);
        row["name"] = QString::fromStdString(track.name);
        // A multi-output instrument's channel (wave 5.2) names what feeds it.
        if (track.source && track.source->track < song_.tracks.size())
            row["instrument"] = QString("%1 · AUX %2")
                                    .arg(QString::fromStdString(song_.tracks[track.source->track].name))
                                    .arg(track.source->output);
        else
            row["instrument"] = instrument_label(track.instrument);
        row["hasInstrument"] = !track.instrument.format.empty();
        row["fed"] = track.source.has_value();
        // The aux outputs its instrument declares, each with whether it is
        // already broken out to a channel.
        QVariantList outputs;
        for (int output = 1; output <= instrumentOutputs(static_cast<int>(index)); ++output) {
            const blokkily::InstrumentOutput which{static_cast<std::uint32_t>(index),
                                                   static_cast<std::uint32_t>(output)};
            const bool routed = std::any_of(song_.tracks.begin(), song_.tracks.end(),
                                            [&](const blokkily::Track& other) {
                                                return other.source == which;
                                            });
            outputs.push_back(QVariantMap{{"output", output}, {"routed", routed}});
        }
        row["outputs"] = outputs;
        row["gainDb"] = track.mix.gain_db;
        row["gainText"] = gain_readout(track.mix.gain_db);
        row["pan"] = track.mix.pan;
        row["panText"] = pan_readout(track.mix.pan);
        row["mute"] = track.mix.mute;
        row["solo"] = track.mix.solo;
        row["audible"] = blokkily::audible(song_.heard_strip(index), solo);
        row["selected"] = static_cast<int>(index) == selected_track_;
        row["automationMode"] = automationMode(static_cast<int>(index));
        row["peak"] = index < peaks_.size() ? static_cast<double>(peaks_[index]) : 0.0;
        // A meter reads in decibels; the bar is that mapped onto the last 60 dB.
        const double peak_db = blokkily::linear_to_db(row["peak"].toDouble());
        row["peakFraction"] = qBound(0.0, (peak_db + 60.0) / 60.0, 1.0);
        row["gainFraction"] = qBound(0.0, (track.mix.gain_db + 60.0) / 66.0, 1.0);
        row["silent"] = gain.silent();
        // Arm and input: the strip's R and its channel.
        row["armed"] = track.input.armed;
        row["inputChannel"] = track.input.midi_channel + 1;
        row["channelText"] = track.input.midi_channel < 0
                                 ? QStringLiteral("ALL")
                                 : QString("CH %1").arg(track.input.midi_channel + 1);
        row["routable"] = index < blokkily::routable_tracks;
        // Audio input (item 3.2): what the input and monitor chips say.
        row["inputText"] = audioInputText(track.input);
        row["monitorText"] = monitorText(track.input);
        row["takesAudio"] = blokkily::takes_audio(track.input);
        // Whether the input is heard through the track now.
        row["monitoring"] = blokkily::takes_audio(track.input) &&
                            (track.input.monitor == blokkily::TrackInput::Monitor::on ||
                             (track.input.monitor == blokkily::TrackInput::Monitor::automatic &&
                              track.input.armed));
        row["inserts"] = static_cast<int>(track.inserts.size());
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
        row["bar"] = barAt(clip.start);
        const auto span = clip.span(song_.patterns[clip.pattern].pattern.length());
        // The bars the clip starts in, counted by the meter it plays through.
        row["bars"] = std::max(1, barAt(clip.start + span - 1) - barAt(clip.start) + 1);
        rows.push_back(row);
    }
    return rows;
}

void SongModel::selectTrack(int track) {
    if (!validTrack(track)) return;
    // Choosing a track also points the effect rack back at its inserts.
    if (track == selected_track_ && rack_kind_ == blokkily::BusKind::track) return;
    selected_track_ = track;
    rack_kind_ = blokkily::BusKind::track;
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

void SongModel::addPattern(int bar) {
    checkpoint();
    // As long as the bar it is made for, so it fills that bar exactly.
    const auto length = std::max<blokkily::Tick>(1, barTicks(std::max(0, bar)));
    song_.patterns.push_back({QString("PATTERN %1").arg(song_.patterns.size() + 1).toStdString(),
                              blokkily::Pattern(length, 480)});
    current_pattern_ = static_cast<int>(song_.patterns.size()) - 1;
    notifyStructureChanged();
}

void SongModel::replace(blokkily::Song song) {
    song_ = std::move(song);
    // A new song is no take's: nothing it is given is recorded into one.
    capturing_ = false;
    take_recorded_ = false;
    if (song_.patterns.empty()) song_.patterns.push_back({"PATTERN 1", blokkily::Pattern(1920, 480)});
    if (song_.tracks.empty()) song_.tracks.push_back({"TRACK 1", {}, {}});
    current_pattern_ = 0;
    selected_track_ = 0;
    peaks_.assign(song_.tracks.size(), 0.0F);
    clearHistory();
    emit tuningChanged();
    emit timebaseChanged();
    emit metronomeChanged();
    notifyStructureChanged();
}

void SongModel::setInstrument(int track, const blokkily::InstrumentSlot& slot) {
    if (!validTrack(track)) return;
    checkpoint();
    auto& changed = song_.tracks[static_cast<std::size_t>(track)];
    changed.instrument = slot;
    // A channel given an instrument of its own plays that instead (wave 5.2).
    if (!slot.format.empty()) changed.source.reset();
    notifyStructureChanged();
}

bool SongModel::setInstrumentState(int track, std::vector<std::byte> state,
                                   const QString& merge) {
    if (!validTrack(track)) return false;
    auto& slot = song_.tracks[static_cast<std::size_t>(track)].instrument;
    if (slot.format.empty()) return false;
    if (slot.state == state) return true;
    checkpoint(merge);
    slot.state = std::move(state);
    emit songChanged();
    return true;
}

void SongModel::refreshStructure() { notifyStructureChanged(); }

void SongModel::notifyStructureChanged() {
    emit structureChanged();
    emit songChanged();
    emit audioClipsChanged();
    emit modulationChanged();
}

namespace {
// A name as the interface shows it: trimmed, capitals, and not so long that it
// cannot fit a clip.
std::string display_name(const QString& name) {
    return name.trimmed().left(24).toUpper().toStdString();
}
} // namespace

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
    // The launcher's cells follow the same rule: a cell of the pattern is
    // emptied, a cell of a later one moves down with it.
    song_.launcher.remove_pattern(removed);
    if (current_pattern_ >= index && current_pattern_ > 0) --current_pattern_;
    notifyStructureChanged();
    return true;
}

bool SongModel::deleteTrack(int track) {
    if (!validTrack(track) || song_.tracks.size() < 2) return false;
    checkpoint();
    const auto removed = static_cast<std::size_t>(track);
    // The song re-indexes everything that names a track (clips, audio clips,
    // sends, automation lanes) in one place, and says where each old track
    // went; the engine rebuild follows that same map to adopt instances.
    auto remap = song_.remove_track(removed);
    if (remap.empty()) return false;
    if (removed < peaks_.size()) peaks_.erase(peaks_.begin() + track);
    if (selected_track_ >= track && selected_track_ > 0) --selected_track_;
    track_remap_ = std::move(remap);
    notifyStructureChanged();
    track_remap_.reset();
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
