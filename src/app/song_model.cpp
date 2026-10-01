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
    // Each pattern becomes a section with a part for every track; BASS gets
    // its own copy of the CHORUS it shares with DRUMS.
    song_.adopt_sections();
}

bool SongModel::validTrack(int track) const {
    return track >= 0 && static_cast<std::size_t>(track) < song_.tracks.size();
}

blokkily::Pattern& SongModel::editPattern() {
    return song_.patterns[static_cast<std::size_t>(currentPattern())].pattern;
}

const blokkily::Pattern& SongModel::editPattern() const {
    return song_.patterns[static_cast<std::size_t>(currentPattern())].pattern;
}

int SongModel::currentPattern() const {
    const auto part = song_.part(static_cast<std::size_t>(current_section_),
                                 static_cast<std::size_t>(std::max(0, selected_track_)));
    return part ? static_cast<int>(*part) : 0;
}

int SongModel::partOf(int section, int track) const {
    if (section < 0 || !validTrack(track)) return -1;
    const auto part = song_.part(static_cast<std::size_t>(section), static_cast<std::size_t>(track));
    return part ? static_cast<int>(*part) : -1;
}

QVariantList SongModel::sections() const {
    QVariantList rows;
    for (std::size_t index = 0; index < song_.sections.size(); ++index) {
        int events = 0;
        for (const auto& slot : song_.patterns)
            if (slot.section == index) events += static_cast<int>(slot.pattern.events().size());
        QVariantMap row;
        row["index"] = static_cast<int>(index);
        row["name"] = QString::fromStdString(song_.sections[index].name);
        row["events"] = events;
        row["current"] = static_cast<int>(index) == current_section_;
        rows.push_back(row);
    }
    return rows;
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
        row["section"] = static_cast<int>(song_.patterns[index].section);
        row["track"] = static_cast<int>(song_.patterns[index].track);
        row["current"] = static_cast<int>(index) == currentPattern();
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
    // The open section stays open: the editors now show this track's part of
    // it, so moving between stems never changes what is being worked on.
    selected_track_ = track;
    rack_kind_ = blokkily::BusKind::track;
    emit songChanged();
}

void SongModel::selectPattern(int pattern) {
    if (pattern < 0 || static_cast<std::size_t>(pattern) >= song_.patterns.size()) return;
    if (pattern == currentPattern()) return;
    const auto& slot = song_.patterns[static_cast<std::size_t>(pattern)];
    current_section_ = static_cast<int>(slot.section);
    selected_track_ = static_cast<int>(slot.track);
    emit songChanged();
}

void SongModel::selectSection(int section) {
    if (section < 0 || static_cast<std::size_t>(section) >= song_.sections.size()) return;
    if (section == current_section_) return;
    current_section_ = section;
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
    // An empty part in every section, playing wherever its section does, so
    // the new track can be written to straight away.
    song_.add_track_parts(song_.tracks.size() - 1);
    peaks_.push_back(0.0F);
    selected_track_ = static_cast<int>(song_.tracks.size()) - 1;
    notifyStructureChanged();
}

void SongModel::addPattern(int bar) {
    checkpoint();
    // As long as the bar it is made for, so it fills that bar exactly.
    const auto length = std::max<blokkily::Tick>(1, barTicks(std::max(0, bar)));
    current_section_ = static_cast<int>(song_.add_section(
        QString("PATTERN %1").arg(song_.sections.size() + 1).toStdString(), length, 480));
    notifyStructureChanged();
}

void SongModel::replace(blokkily::Song song) {
    song_ = std::move(song);
    // A new song is no take's: nothing it is given is recorded into one.
    capturing_ = false;
    take_recorded_ = false;
    if (song_.patterns.empty()) song_.patterns.push_back({"PATTERN 1", blokkily::Pattern(1920, 480)});
    if (song_.tracks.empty()) song_.tracks.push_back({"TRACK 1", {}, {}});
    // A song from before sections is given them here, playing as it did.
    song_.adopt_sections();
    current_section_ = 0;
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
    const auto source = static_cast<std::size_t>(current_section_);
    // "VERSE" becomes "VERSE 2", then "VERSE 3": a copy is named after what it
    // copies, and never collides with a name already in the song.
    const QString base = QString::fromStdString(song_.sections[source].name);
    std::string name;
    for (int suffix = 2;; ++suffix) {
        const auto candidate = QString("%1 %2").arg(base).arg(suffix).toStdString();
        const bool taken = std::any_of(song_.sections.begin(), song_.sections.end(),
            [&](const blokkily::Section& section) { return section.name == candidate; });
        if (!taken) { name = candidate; break; }
    }
    const auto& first = editPattern();
    const auto copy = song_.add_section(name, first.length(), first.ticks_per_beat());
    // Every track's part is copied, notes and controller movements alike.
    for (std::size_t track = 0; track < song_.tracks.size(); ++track) {
        const auto from = song_.part(source, track);
        const auto to = song_.part(copy, track);
        if (from && to) song_.patterns[*to].pattern = song_.patterns[*from].pattern;
    }
    current_section_ = static_cast<int>(copy);
    notifyStructureChanged();
}

void SongModel::clearPattern() {
    const auto section = static_cast<std::size_t>(current_section_);
    const bool empty = std::all_of(song_.patterns.begin(), song_.patterns.end(),
        [section](const blokkily::PatternSlot& slot) {
            return slot.section != section ||
                   (slot.pattern.events().empty() && slot.pattern.continuous().empty());
        });
    if (empty) return;
    checkpoint();
    for (auto& slot : song_.patterns)
        if (slot.section == section)
            slot.pattern = blokkily::Pattern(slot.pattern.length(), slot.pattern.ticks_per_beat());
    notifyStructureChanged();
}

bool SongModel::deletePattern(int section) {
    // A song always has a section open, so the last one stays.
    if (section < 0 || static_cast<std::size_t>(section) >= song_.sections.size() ||
        song_.sections.size() < 2)
        return false;
    checkpoint();
    // Its parts go, with their clips and launcher cells; everything naming a
    // later pattern or section still names the same one after the lists close
    // up.
    (void)song_.remove_section(static_cast<std::size_t>(section));
    if (current_section_ >= section && current_section_ > 0) --current_section_;
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

void SongModel::renamePattern(int section, const QString& name) {
    if (section < 0 || static_cast<std::size_t>(section) >= song_.sections.size()) return;
    const auto wanted = display_name(name);
    auto& target = song_.sections[static_cast<std::size_t>(section)];
    if (wanted.empty() || wanted == target.name) return;
    checkpoint();
    target.name = wanted;
    // Each part carries its section's name, so a lane's clip reads the same.
    for (auto& slot : song_.patterns)
        if (slot.section == static_cast<std::size_t>(section)) slot.name = wanted;
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
