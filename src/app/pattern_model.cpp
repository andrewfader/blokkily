#include "pattern_model.hpp"
#include "blokkily/instruments/soundfont_catalog.hpp"
#include "blokkily/instruments/soundfont_synth.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/audio/bounce.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QString>
#include <QVariantMap>
#include <QUrl>

#include <cstring>
#include <system_error>

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <functional>
#include <vector>

namespace {
QVariantMap plugin_entry(const QString& format, const std::string& name,
                         const std::string& vendor, const std::string& path = {},
                         const std::string& identifier = {}, int index = 0) {
    return {{"format", format},
            {"name", QString::fromStdString(name)},
            {"vendor", QString::fromStdString(vendor)},
            {"path", QString::fromStdString(path)},
            {"identifier", QString::fromStdString(identifier)},
            {"index", index}};
}

QString local_path(const QString& value) {
    const QUrl url(value);
    return url.isLocalFile() ? url.toLocalFile() : value;
}

QString hex2(int value) {
    return QString("%1").arg(qBound(0, value, 255), 2, 16, QChar('0')).toUpper();
}

// Velocity has exactly one numeric meaning in this app: MIDI units. Computing
// it in one place keeps the tracker and the inspector from disagreeing.
int velocity_units(float velocity) {
    return qBound(0, qRound(static_cast<double>(velocity) * 127.0), 127);
}

// The pitch a step carries, said in the song's tuning. A chord is one event
// rather than a stack of notes, so it is named by its lowest voice and how many
// voices stand on it; every editor asks here so none of them can disagree.
QString pitch_label(const SongModel* song, const blokkily::Trigger& trigger) {
    if (song == nullptr) return QStringLiteral("---");
    if (const auto* note = std::get_if<blokkily::Note>(&trigger.musical_data))
        return song->pitchName(note->key, note->cents);
    const auto& chord = std::get<blokkily::Chord>(trigger.musical_data);
    const double root_cents = chord.cents.empty() ? 0.0 : chord.cents.front();
    return QString("%1+%2").arg(song->pitchName(chord.root, root_cents))
                           .arg(chord.intervals.size() > 1 ? chord.intervals.size() - 1 : 0);
}

// The key an instrument is told for the step's lowest voice, and how many
// voices it holds.
int pitch_key(const blokkily::Trigger& trigger) {
    if (const auto* note = std::get_if<blokkily::Note>(&trigger.musical_data)) return note->key;
    return std::get<blokkily::Chord>(trigger.musical_data).root;
}

double pitch_cents(const blokkily::Trigger& trigger) {
    if (const auto* note = std::get_if<blokkily::Note>(&trigger.musical_data)) return note->cents;
    const auto& chord = std::get<blokkily::Chord>(trigger.musical_data);
    return chord.cents.empty() ? 0.0 : chord.cents.front();
}

int pitch_voices(const blokkily::Trigger& trigger) {
    if (std::holds_alternative<blokkily::Note>(trigger.musical_data)) return 1;
    return static_cast<int>(std::get<blokkily::Chord>(trigger.musical_data).intervals.size());
}

QString lock_text(const blokkily::Trigger& trigger) {
    if (trigger.locks.empty()) return QStringLiteral("---");
    const auto& lock = trigger.locks.front();
    // Tracker-style effect column: kind, parameter index, then the value.
    return QString("%1%2%3")
        .arg(lock.kind == blokkily::ParameterLock::Kind::modulation ? "M" : "A")
        .arg(lock.parameter_index, 1, 16)
        .arg(hex2(qRound(lock.value * 127.0)));
}
}

PatternModel::PatternModel(SongModel* song, QObject* parent)
    : QAbstractListModel(parent), song_(song) {
    selected_step_ = 8;
    if (song_ != nullptr)
        QObject::connect(song_, &SongModel::songChanged, this, &PatternModel::refresh);
}

// The editors project whichever pattern the song has open, so switching
// patterns in the arrangement moves all three views at once.
blokkily::Pattern& PatternModel::pattern() { return song_->editPattern(); }
const blokkily::Pattern& PatternModel::pattern() const { return song_->editPattern(); }

void PatternModel::refresh() {
    beginResetModel();
    endResetModel();
    emit patternChanged();
    emit selectionChanged();
}

int PatternModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(pattern().events().size());
}

QVariant PatternModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& event = pattern().events()[static_cast<std::size_t>(index.row())];
    const auto* note = std::get_if<blokkily::Note>(&event.musical_data);
    switch (role) {
    case IdRole: return QVariant::fromValue<qulonglong>(event.id);
    case StepRole: return static_cast<int>(event.start / ticks_per_step);
    case KeyRole: return pitch_key(event);
    case NameRole: return pitch_label(song_, event);
    case DurationRole: return static_cast<int>(event.duration);
    case VelocityRole: return hex2(velocity_units(note == nullptr ? 0.8F : note->velocity));
    case LockRole: return lock_text(event);
    default: return {};
    }
}

QHash<int, QByteArray> PatternModel::roleNames() const {
    return {{IdRole, "eventId"}, {StepRole, "step"}, {KeyRole, "key"},
            {NameRole, "noteName"}, {DurationRole, "duration"},
            {VelocityRole, "velocityHex"}, {LockRole, "lockText"}};
}

const blokkily::Trigger* PatternModel::triggerAt(int step) const {
    for (const auto& event : pattern().events())
        if (event.start / ticks_per_step == step) return &event;
    return nullptr;
}

// One row projection feeding both the step grid and the tracker, so the two
// editors can never disagree about what a step contains.
QVariantList PatternModel::steps() const {
    QVariantList rows;
    rows.reserve(step_count);
    for (int step = 0; step < step_count; ++step) {
        const auto* trigger = triggerAt(step);
        QVariantMap row;
        row["active"] = trigger != nullptr;
        row["selected"] = step == selected_step_;
        if (trigger == nullptr) {
            row["noteName"] = "---";
            row["velocityHex"] = "--";
            row["lockText"] = "---";
            row["velocity"] = 0.0;
            row["velocityUnits"] = 0;
            row["hasLock"] = false;
            row["key"] = 0;
            row["cents"] = 0.0;
            row["voices"] = 0;
            row["ratchets"] = 0;
        } else {
            const auto* note = std::get_if<blokkily::Note>(&trigger->musical_data);
            const float velocity = note == nullptr ? 0.8F : note->velocity;
            row["noteName"] = pitch_label(song_, *trigger);
            row["velocityHex"] = hex2(velocity_units(velocity));
            row["velocityUnits"] = velocity_units(velocity);
            row["lockText"] = lock_text(*trigger);
            row["velocity"] = static_cast<double>(velocity);
            row["hasLock"] = !trigger->locks.empty();
            row["key"] = pitch_key(*trigger);
            row["cents"] = pitch_cents(*trigger);
            row["voices"] = pitch_voices(*trigger);
            row["ratchets"] = static_cast<int>(trigger->ratchets);
        }
        rows.push_back(row);
    }
    return rows;
}

QVariantMap PatternModel::selected() const {
    QVariantMap detail;
    const auto* trigger = selected_step_ < 0 ? nullptr : triggerAt(selected_step_);
    detail["step"] = selected_step_;
    detail["exists"] = trigger != nullptr;
    if (trigger == nullptr) return detail;
    const auto* note = std::get_if<blokkily::Note>(&trigger->musical_data);
    detail["key"] = pitch_key(*trigger);
    detail["noteName"] = pitch_label(song_, *trigger);
    detail["cents"] = pitch_cents(*trigger);
    detail["voices"] = pitch_voices(*trigger);
    detail["velocity"] = note == nullptr ? 0.8 : static_cast<double>(note->velocity);
    detail["velocityUnits"] = velocity_units(note == nullptr ? 0.8F : note->velocity);
    detail["probability"] = static_cast<double>(trigger->probability);
    detail["ratchets"] = static_cast<int>(trigger->ratchets);
    detail["micro"] = static_cast<int>(trigger->micro_offset);
    detail["loop"] = static_cast<int>(trigger->play_on_loop);
    detail["hasLock"] = !trigger->locks.empty();
    detail["lockText"] = lock_text(*trigger);
    if (!trigger->locks.empty()) {
        const auto& lock = trigger->locks.front();
        detail["lockIndex"] = lock.parameter_index;
        detail["lockValue"] = lock.value;
        detail["lockModulation"] = lock.kind == blokkily::ParameterLock::Kind::modulation;
    } else {
        detail["lockIndex"] = 0;
        detail["lockValue"] = 0.5;
        detail["lockModulation"] = false;
    }
    return detail;
}

// The roll shows at least two octaves and always widens to fit the pattern, so
// no note can be drawn outside the lanes the keyboard gutter labels.
namespace {
constexpr int minimum_roll_span = 24; // never show less than two octaves
}

int PatternModel::lowKey() const {
    int lowest = 127;
    int highest = 0;
    for (const auto& event : pattern().events())
        if (const auto* note = std::get_if<blokkily::Note>(&event.musical_data)) {
            lowest = std::min(lowest, static_cast<int>(note->key));
            highest = std::max(highest, static_cast<int>(note->key));
        }
    if (pattern().events().empty()) { lowest = 48; highest = 60; }
    int low = lowest - 4;
    const int span = (highest + 4) - low;
    if (span < minimum_roll_span) low -= (minimum_roll_span - span) / 2;
    return qBound(0, low, 127 - minimum_roll_span);
}

int PatternModel::highKey() const {
    int lowest = 127;
    int highest = 0;
    for (const auto& event : pattern().events())
        if (const auto* note = std::get_if<blokkily::Note>(&event.musical_data)) {
            lowest = std::min(lowest, static_cast<int>(note->key));
            highest = std::max(highest, static_cast<int>(note->key));
        }
    if (pattern().events().empty()) highest = 60;
    return qBound(lowKey() + minimum_roll_span, highest + 4, 127);
}

bool PatternModel::hasStep(int step) const { return triggerAt(step) != nullptr; }

void PatternModel::selectStep(int step) {
    // Selection is not an edit: the arrangement still plays what it played, so
    // nothing here may reach the engine and interrupt it.
    selected_step_ = qBound(-1, step, step_count - 1);
    emit selectionChanged();
}

void PatternModel::toggleStep(int step, int key) {
    if (const auto* existing = triggerAt(step)) {
        const auto id = existing->id;
        beginResetModel();
        (void)pattern().remove(id);
        endResetModel();
        selected_step_ = step;
        emit patternChanged();
        emit contentChanged();
        emit selectionChanged();
        return;
    }
    blokkily::Trigger trigger;
    trigger.start = step * ticks_per_step;
    trigger.duration = 96;
    trigger.musical_data = blokkily::Note{static_cast<std::int16_t>(key), 0.9F, 0.0F};
    beginResetModel();
    (void)pattern().add(trigger);
    endResetModel();
    selected_step_ = step;
    emit patternChanged();
    emit contentChanged();
    emit selectionChanged();
}

// Writing a pitch onto a step is the same edit whichever editor asks for it,
// so the tracker's note column and the piano roll's lanes both come through
// here rather than each growing an editing path of its own.
void PatternModel::setStepKey(int step, int key) {
    if (step < 0 || step >= step_count) return;
    const int bounded = qBound(0, key, 127);
    // A pitch written in the roll or the tracker is a twelve-tone key, which in
    // a tuning that is not twelve-tone still means the key the instrument is
    // told. The retune the step already carried is kept, so re-pitching a
    // microtonal step does not quietly straighten it back onto equal semitones.
    const auto* existing = triggerAt(step);
    const double cents = existing == nullptr ? 0.0 : pitch_cents(*existing);
    const float velocity = existing == nullptr
                               ? 0.9F
                               : (std::holds_alternative<blokkily::Note>(existing->musical_data)
                                      ? std::get<blokkily::Note>(existing->musical_data).velocity
                                      : 0.9F);
    placeNote(step, blokkily::Note{static_cast<std::int16_t>(bounded), velocity, 0.0F, cents});
}

void PatternModel::clearStep(int step) {
    const auto* existing = triggerAt(step);
    if (existing == nullptr) return;
    const auto id = existing->id;
    beginResetModel();
    (void)pattern().remove(id);
    endResetModel();
    selected_step_ = step;
    emit patternChanged();
    emit contentChanged();
    emit selectionChanged();
}

std::vector<blokkily::TunedPitch> PatternModel::pitchesAt(int step) const {
    const auto* trigger = triggerAt(step);
    if (trigger == nullptr) return {};
    if (const auto* note = std::get_if<blokkily::Note>(&trigger->musical_data))
        return {{note->key, note->cents}};
    const auto& chord = std::get<blokkily::Chord>(trigger->musical_data);
    std::vector<blokkily::TunedPitch> pitches;
    pitches.reserve(chord.intervals.size());
    for (std::size_t voice = 0; voice < chord.intervals.size(); ++voice)
        pitches.push_back({static_cast<std::int16_t>(chord.root + chord.intervals[voice]),
                           voice < chord.cents.size() ? chord.cents[voice] : 0.0});
    return pitches;
}

namespace {
// A step written by a playable surface. Everything but the pitch is the same
// for a note and for a chord, so both go on through one place.
blokkily::Trigger played_trigger(int step, blokkily::Tick ticks_per_step) {
    blokkily::Trigger trigger;
    trigger.start = step * ticks_per_step;
    trigger.duration = 96;
    return trigger;
}
} // namespace

void PatternModel::placeNote(int step, const blokkily::Note& note) {
    if (step < 0 || step >= step_count) return;
    auto trigger = played_trigger(step, ticks_per_step);
    trigger.musical_data = note;
    beginResetModel();
    if (const auto* existing = triggerAt(step)) {
        // Playing over a step replaces its pitch and keeps everything else the
        // producer set on it: its locks, its ratchets, its microtiming.
        auto replacement = *existing;
        replacement.musical_data = note;
        (void)pattern().update(replacement.id, replacement);
    } else {
        (void)pattern().add(trigger);
    }
    endResetModel();
    selected_step_ = step;
    emit patternChanged();
    emit contentChanged();
    emit selectionChanged();
}

void PatternModel::placeChord(int step, const blokkily::Chord& chord) {
    if (step < 0 || step >= step_count) return;
    auto trigger = played_trigger(step, ticks_per_step);
    trigger.musical_data = chord;
    beginResetModel();
    if (const auto* existing = triggerAt(step)) {
        auto replacement = *existing;
        replacement.musical_data = chord;
        (void)pattern().update(replacement.id, replacement);
    } else {
        (void)pattern().add(trigger);
    }
    endResetModel();
    selected_step_ = step;
    emit patternChanged();
    emit contentChanged();
    emit selectionChanged();
}

void PatternModel::mutate(int step, const std::function<void(blokkily::Trigger&)>& edit) {
    const auto* existing = triggerAt(step);
    if (existing == nullptr) return;
    blokkily::Trigger replacement = *existing;
    edit(replacement);
    beginResetModel();
    (void)pattern().update(replacement.id, replacement);
    endResetModel();
    emit patternChanged();
    emit contentChanged();
    emit selectionChanged();
}

void PatternModel::transposeSelected(int semitones) {
    mutate(selected_step_, [semitones](blokkily::Trigger& trigger) {
        if (auto* note = std::get_if<blokkily::Note>(&trigger.musical_data)) {
            note->key = static_cast<std::int16_t>(qBound(0, note->key + semitones, 127));
            return;
        }
        // A chord moves as a whole: its voices are intervals above its root, so
        // transposing the root carries the harmony with it.
        auto& chord = std::get<blokkily::Chord>(trigger.musical_data);
        chord.root = static_cast<std::int16_t>(qBound(0, chord.root + semitones, 127));
    });
}

void PatternModel::setSelectedVelocity(double velocity) {
    mutate(selected_step_, [velocity](blokkily::Trigger& trigger) {
        if (auto* note = std::get_if<blokkily::Note>(&trigger.musical_data))
            note->velocity = static_cast<float>(qBound(0.0, velocity, 1.0));
    });
}

void PatternModel::setSelectedProbability(double probability) {
    mutate(selected_step_, [probability](blokkily::Trigger& trigger) {
        trigger.probability = static_cast<float>(qBound(0.0, probability, 1.0));
    });
}

void PatternModel::setSelectedRatchets(int ratchets) {
    mutate(selected_step_, [ratchets](blokkily::Trigger& trigger) {
        trigger.ratchets = static_cast<std::uint8_t>(qBound(1, ratchets, 16));
    });
}

void PatternModel::setSelectedMicroOffset(int ticks) {
    mutate(selected_step_, [ticks](blokkily::Trigger& trigger) {
        trigger.micro_offset = qBound(-59, ticks, 59);
    });
}

void PatternModel::setSelectedPlayOnLoop(int loop) {
    mutate(selected_step_, [loop](blokkily::Trigger& trigger) {
        trigger.play_on_loop = static_cast<std::uint8_t>(qBound(0, loop, 8));
    });
}

void PatternModel::setSelectedLock(int index, double value, bool modulation) {
    mutate(selected_step_, [index, value, modulation](blokkily::Trigger& trigger) {
        trigger.locks = {{"level", qBound(0, index, 15), qBound(0.0, value, 1.0),
                          modulation ? blokkily::ParameterLock::Kind::modulation
                                     : blokkily::ParameterLock::Kind::automation}};
    });
}

void PatternModel::clearSelectedLock() {
    mutate(selected_step_, [](blokkily::Trigger& trigger) { trigger.locks.clear(); });
}

void PatternModel::replace(blokkily::Pattern replacement) {
    beginResetModel();
    pattern() = std::move(replacement);
    endResetModel();
    emit patternChanged();
    emit contentChanged();
    emit selectionChanged();
}

Transport::Transport(QObject* parent) : QObject(parent) {
    timer_.setInterval(16);
    QObject::connect(&timer_, &QTimer::timeout, this, &Transport::tick);
}

// Bar.beat.sixteenth across the whole arrangement, which is what a transport
// readout means. A step is a sixteenth note, so sixteen of them make a bar.
QString Transport::position() const {
    const auto absolute = static_cast<int>(step_position_);
    const int step = absolute % steps_per_bar;
    return QString("%1.%2.%3")
        .arg(absolute / steps_per_bar + 1).arg(step / 4 + 1).arg(step % 4 + 1);
}

void Transport::setSongBars(int bars) {
    bars_ = std::max(1, bars);
    locate(step_position_);
}

void Transport::followSamples(std::uint64_t samples, double sample_rate) {
    following_ = true;
    if (sample_rate <= 0.0 || bpm_ <= 0.0) return;
    // Four sixteenths to the beat.
    const double samples_per_step = sample_rate * 60.0 / (bpm_ * 4.0);
    if (samples_per_step <= 0.0) return;
    locate(static_cast<double>(samples) / samples_per_step);
}

void Transport::releaseFollowing() { following_ = false; }

void Transport::play() {
    if (playing_) return;
    playing_ = true;
    clock_.restart();
    timer_.start();
    emit changed();
}

void Transport::stop() {
    if (!playing_) return;
    playing_ = false;
    timer_.stop();
    emit changed();
}

void Transport::toggle() { playing_ ? stop() : play(); }

void Transport::rewind() {
    step_position_ = 0.0;
    clock_.restart();
    emit changed();
}

void Transport::locate(double step) {
    const double length = steps_per_bar * bars_;
    step_position_ = std::fmod(std::fmod(step, length) + length, length);
    clock_.restart();
    emit changed();
}

void Transport::setBpm(double bpm) {
    bpm_ = qBound(20.0, bpm, 300.0);
    emit changed();
}

void Transport::tick() {
    // While an engine is bound the playhead follows its sample position; this
    // free-running clock only drives the interface when nothing is rendering.
    if (following_) return;
    // A 16th note per step: one beat covers four steps.
    const double elapsed = static_cast<double>(clock_.restart()) / 1000.0;
    locate(step_position_ + elapsed * bpm_ / 60.0 * 4.0);
}

AppController::AppController(SongModel* song, PatternModel* pattern, Transport* transport,
                             QObject* parent, std::unique_ptr<blokkily::RtAudioOutput> output)
    : QObject(parent), song_(song), pattern_(pattern), transport_(transport),
      audio_output_(std::move(output)) {
    // What the browser lists follows both what the scan found and what the
    // producer has typed, so a plugin arriving mid-scan reaches a filtered
    // browser too.
    QObject::connect(this, &AppController::pluginsChanged,
                     this, &AppController::browserChanged);
    if (pattern_ != nullptr)
        QObject::connect(pattern_, &PatternModel::contentChanged, this, [this] {
            // Editing a step changes what the arrangement plays, not what plays
            // it, so the recompiled timeline is handed to the running engine.
            // The song keeps going from where it was, on the instruments it
            // already has, and the next block plays the edit.
            if (engine_) {
                if (builtFromCurrentInstruments()) (void)refreshArrangement();
                else (void)rebuildEngine();
            }
        });
    if (song_ != nullptr) {
        QObject::connect(song_, &SongModel::structureChanged, this, [this] {
            // A clip moved on the timeline is the same kind of change as a
            // step edit. A track added or an instrument swapped is not: that
            // needs a graph the running engine does not have.
            if (engine_ && builtFromCurrentInstruments()) {
                (void)refreshArrangement();
                return;
            }
            // A track that has just been given an instrument needs an engine
            // even if none existed: a keyboard must sound before playback.
            if (engine_ || !instruments().empty()) (void)rebuildEngine();
        });
        // A fader move only changes gains, so it reaches the running engine
        // without rebuilding the graph or interrupting playback.
        QObject::connect(song_, &SongModel::mixChanged, this, &AppController::applyMix);
    }
    // A held note is let go on a timer rather than on a second press, so a
    // keyboard cannot leave a voice sounding after the finger has left it.
    audition_timer_.setSingleShot(true);
    audition_timer_.setInterval(450);
    QObject::connect(&audition_timer_, &QTimer::timeout, this,
                     &AppController::releaseSoundingNotes);
    meter_timer_.setInterval(33);
    QObject::connect(&meter_timer_, &QTimer::timeout, this, &AppController::pollMeters);
    // A candidate that stops answering is given up on rather than waited for.
    scan_deadline_.setSingleShot(true);
    QObject::connect(&scan_deadline_, &QTimer::timeout, this, [this] {
        if (!scanner_) return;
        scan_expired_ = true;
        scanner_->kill();
    });
}

AppController::~AppController() {
    if (scanner_) {
        scanner_->kill();
        scanner_->waitForFinished(1000);
    }
}

// Nothing is left holding a note across a rebuild: the engine that was told to
// sound it no longer exists, so the release would land on an instrument that
// never heard the press.
void AppController::forgetSoundingNotes() {
    sounding_.clear();
    audition_timer_.stop();
}

// Lets go of every note the keyboard is holding, on the track that was told to
// sound it rather than on whichever one happens to be selected now.
void AppController::releaseSoundingNotes() {
    if (engine_ != nullptr)
        for (const auto& held : sounding_)
            (void)engine_->play_live(static_cast<std::size_t>(audition_track_),
                                     {blokkily::PluginEvent::Type::note_off, 0, held.key,
                                      0.0, held.cents});
    sounding_.clear();
}

bool AppController::auditionPitches(const std::vector<blokkily::TunedPitch>& pitches,
                                    double velocity) {
    if (song_ == nullptr) return false;
    // A press while an engine exists is heard immediately; without one there is
    // nothing to sound through, and the surface still writes what was played.
    if (!engine_ && !instruments().empty()) (void)rebuildEngine();
    if (!engine_) return false;
    // Whatever was still sounding is let go first, so a run of presses does not
    // pile voices up on the instrument. It is released on the track that was
    // told to sound it: a producer who changes track mid-press would otherwise
    // leave a voice held on the instrument they just left.
    releaseSoundingNotes();
    const auto track = static_cast<std::size_t>(song_->selectedTrack());
    if (track >= engine_->track_count() || !engine_->has_instrument(track)) return false;
    std::string error;
    if (audio_output_ && !audio_output_->start(&error)) {
        status_ = QString("Audio start failed · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        return false;
    }
    meter_timer_.start();
    audition_track_ = song_->selectedTrack();
    for (const auto& pitch : pitches) {
        if (!engine_->play_live(track, {blokkily::PluginEvent::Type::note_on, 0, pitch.key,
                                        velocity, pitch.cents}))
            break;
        sounding_.push_back(pitch);
    }
    audition_timer_.start();
    return !sounding_.empty();
}

QString AppController::activeInstrument() const {
    if (song_ == nullptr) return QStringLiteral("Choose an instrument");
    const auto tracks = song_->tracks();
    const int selected = song_->selectedTrack();
    if (selected < 0 || selected >= tracks.size()) return QStringLiteral("Choose an instrument");
    const auto row = tracks.at(selected).toMap();
    // The strip's own name is shown beside this, so the label names only the
    // instrument the selected track carries.
    if (!row.value("hasInstrument").toBool()) return QStringLiteral("Choose an instrument");
    return row.value("instrument").toString();
}

std::vector<blokkily::InstrumentSlot> AppController::instruments() const {
    // `slots` is a Qt keyword, so the local carries the longer name.
    std::vector<blokkily::InstrumentSlot> assigned;
    if (song_ == nullptr) return assigned;
    for (const auto& track : song_->song().tracks)
        if (!track.instrument.format.empty()) assigned.push_back(track.instrument);
    return assigned;
}

void AppController::scanPlugins() {
    beginScan(blokkily::ClapCatalog::system_paths(),
              blokkily::Vst3PluginInstance::system_paths(),
              blokkily::SoundFontCatalog::system_paths());
}

void AppController::rescanPlugins() {
    beginScan(blokkily::ClapCatalog::system_paths(),
              blokkily::Vst3PluginInstance::system_paths(),
              blokkily::SoundFontCatalog::system_paths(), true);
}

QString AppController::scanHelperPath() const {
    if (const auto override_path = qEnvironmentVariable("BLOKKILY_SCAN_HELPER");
        !override_path.isEmpty())
        return override_path;
    return QCoreApplication::applicationDirPath() + "/blokkily_scan";
}

QString AppController::scanCachePath() const {
    if (const auto override_path = qEnvironmentVariable("BLOKKILY_SCAN_CACHE");
        !override_path.isEmpty())
        return override_path;
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
           "/plugin-scan-cache.txt";
}

void AppController::loadScanCache() {
    if (scan_cache_loaded_) return;
    scan_cache_loaded_ = true;
    QFile file(scanCachePath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    const auto text = file.readAll();
    scan_cache_ = blokkily::read_scan_cache(std::string_view(text.constData(),
                                                             static_cast<std::size_t>(text.size())));
}

void AppController::saveScanCache() const {
    const QString path = scanCachePath();
    QDir{}.mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return;
    const auto text = blokkily::write_scan_cache(scan_cache_);
    file.write(text.data(), static_cast<qint64>(text.size()));
}

blokkily::ScanCacheEntry* AppController::cachedScan(const blokkily::ScanCandidate& candidate) {
    for (auto& entry : scan_cache_) {
        if (entry.candidate.format != candidate.format ||
            entry.candidate.path != candidate.path)
            continue;
        // A plugin replaced since it was scanned is described again: the cache
        // remembers a file, not a name.
        if (entry.stamp != blokkily::scan_stamp(candidate.path) ||
            entry.size != blokkily::scan_size(candidate.path))
            return nullptr;
        return &entry;
    }
    return nullptr;
}

void AppController::beginScan(const std::vector<std::filesystem::path>& clap_paths,
                              const std::vector<std::filesystem::path>& vst3_paths,
                              const std::vector<std::filesystem::path>& soundfont_paths,
                              bool forget_cache) {
    if (scanning_) return;
    loadScanCache();
    if (forget_cache) scan_cache_.clear();

    // SoundFonts are files the host reads itself, so they are listed up front;
    // only formats that mean loading someone else's code go through a helper.
    const auto soundfonts = blokkily::SoundFontCatalog::scan_paths(soundfont_paths);
    plugins_.clear();
    scan_failures_ = 0;
    scan_index_ = 0;
    scan_queue_ = blokkily::enumerate_scan_candidates(clap_paths, vst3_paths);
    scanning_ = true;
    emit scanningChanged();

    soundfont_status_ = QString("%1 installed SoundFont%2")
                            .arg(soundfonts.size()).arg(soundfonts.size() == 1 ? "" : "s");
    emit soundfontStatusChanged();
    for (const auto& soundfont : soundfonts)
        plugins_.push_back(plugin_entry("SF", soundfont.stem().string(),
                                        soundfont.parent_path().string(), soundfont.string()));
    emit pluginsChanged();
    reportScanProgress();
    // Always through the event loop, even when every candidate is already
    // cached: a caller that connects to `scanFinished` after asking for the
    // scan must not miss it.
    QTimer::singleShot(0, this, &AppController::scanNext);
}

void AppController::appendRecords(const std::vector<blokkily::ScanRecord>& records) {
    if (records.empty()) return;
    for (const auto& record : records)
        plugins_.push_back(plugin_entry(QString::fromStdString(record.format), record.name,
                                        record.vendor, record.path, record.identifier,
                                        record.index));
    emit pluginsChanged();
}

void AppController::reportScanProgress() {
    status_ = QString("Scanning %1/%2 — %3 found, %4 failure%5")
                  .arg(scan_index_).arg(scan_queue_.size())
                  .arg(plugins_.size()).arg(scan_failures_)
                  .arg(scan_failures_ == 1 ? "" : "s");
    emit statusChanged();
}

void AppController::scanNext() {
    const QString helper = scanHelperPath();
    while (scan_index_ < scan_queue_.size()) {
        const auto& candidate = scan_queue_[scan_index_];
        if (const auto* cached = cachedScan(candidate)) {
            // Already described, or already known to be unscannable: either way
            // the plugin is not loaded again.
            if (cached->ok) appendRecords(cached->records);
            else ++scan_failures_;
            ++scan_index_;
            continue;
        }
        if (!QFileInfo(helper).isExecutable()) {
            status_ = QString("Plugin scan helper not found at %1").arg(helper);
            emit statusChanged();
            scan_index_ = scan_queue_.size();
            break;
        }
        startScanner(candidate);
        return;
    }
    finishScan();
}

void AppController::startScanner(const blokkily::ScanCandidate& candidate) {
    scan_expired_ = false;
    scanner_ = std::make_unique<QProcess>();
    scanner_->setProgram(scanHelperPath());
    scanner_->setArguments({QString::fromStdString(candidate.format),
                            QString::fromStdString(candidate.path.string())});
    QObject::connect(scanner_.get(), &QProcess::errorOccurred, this,
                     [this](QProcess::ProcessError error) {
                         if (error != QProcess::FailedToStart) return;
                         completeCandidate(false, "scan helper could not be started", {});
                     });
    QObject::connect(scanner_.get(), &QProcess::finished, this,
                     [this](int code, QProcess::ExitStatus exit_status) {
                         if (scan_expired_)
                             return completeCandidate(false, "scan timed out", {});
                         if (exit_status != QProcess::NormalExit)
                             return completeCandidate(false, "scan crashed", {});
                         const auto output = scanner_->readAllStandardOutput();
                         if (code != 0)
                             return completeCandidate(
                                 false, QString::fromUtf8(scanner_->readAllStandardError()).trimmed(),
                                 {});
                         completeCandidate(true, {},
                             blokkily::read_scan_records(std::string_view(
                                 output.constData(), static_cast<std::size_t>(output.size()))));
                     });
    scan_deadline_.start(qEnvironmentVariableIntValue("BLOKKILY_SCAN_TIMEOUT_MS") > 0
                             ? qEnvironmentVariableIntValue("BLOKKILY_SCAN_TIMEOUT_MS")
                             : 15000);
    scanner_->start();
}

void AppController::completeCandidate(bool ok, const QString& failure,
                                      std::vector<blokkily::ScanRecord> records) {
    scan_deadline_.stop();
    if (scanner_) {
        scanner_->disconnect(this);
        scanner_.release()->deleteLater();
    }
    if (scan_index_ >= scan_queue_.size()) return;

    blokkily::ScanCacheEntry entry;
    entry.candidate = scan_queue_[scan_index_];
    entry.stamp = blokkily::scan_stamp(entry.candidate.path);
    entry.size = blokkily::scan_size(entry.candidate.path);
    entry.ok = ok;
    entry.failure = failure.toStdString();
    entry.records = records;
    std::erase_if(scan_cache_, [&entry](const blokkily::ScanCacheEntry& existing) {
        return existing.candidate.format == entry.candidate.format &&
               existing.candidate.path == entry.candidate.path;
    });
    scan_cache_.push_back(std::move(entry));

    if (ok) appendRecords(records);
    else ++scan_failures_;
    ++scan_index_;
    reportScanProgress();
    // Written as the scan goes, so a session closed halfway through does not
    // throw away what has already been learned.
    saveScanCache();
    // Back to the event loop between candidates, so the interface keeps
    // answering while a long scan works through the queue.
    QTimer::singleShot(0, this, &AppController::scanNext);
}

void AppController::finishScan() {
    scanning_ = false;
    saveScanCache();
    int clap = 0;
    int vst3 = 0;
    int soundfonts = 0;
    for (const auto& entry : plugins_) {
        const auto format = entry.toMap().value("format").toString();
        if (format == "CLAP") ++clap;
        else if (format == "VST3") ++vst3;
        else ++soundfonts;
    }
    status_ = QString("Scan: %1 CLAP, %2 VST3, %3 SoundFont, %4 failure%5")
                  .arg(clap).arg(vst3).arg(soundfonts).arg(scan_failures_)
                  .arg(scan_failures_ == 1 ? "" : "s");
    emit statusChanged();
    emit scanningChanged();
    emit scanFinished();
}

void AppController::scanPluginPaths(
    const std::vector<std::filesystem::path>& clap_paths,
    const std::vector<std::filesystem::path>& vst3_paths,
    const std::vector<std::filesystem::path>& soundfont_paths) {
    // CLAP is the first-class format, so it leads the browser; every installed
    // instrument type is still presented through the same list.
    const auto clap = blokkily::ClapCatalog{}.scan_paths(clap_paths);
    const auto vst3 = blokkily::Vst3PluginInstance::scan_paths(vst3_paths);
    const auto soundfonts = blokkily::SoundFontCatalog::scan_paths(soundfont_paths);
    plugins_.clear();
    for (const auto& plugin : clap.plugins)
        plugins_.push_back(plugin_entry("CLAP", plugin.name, plugin.vendor,
                                        plugin.library.string(), plugin.id));
    for (const auto& plugin : vst3)
        plugins_.push_back(plugin_entry("VST3", plugin.name, plugin.manufacturer,
                                        plugin.bundle.string(), plugin.identifier,
                                        static_cast<int>(plugin.index)));
    for (const auto& soundfont : soundfonts)
        plugins_.push_back(plugin_entry("SF", soundfont.stem().string(),
                                        soundfont.parent_path().string(), soundfont.string()));
    soundfont_status_ = QString("%1 installed SoundFont%2")
                            .arg(soundfonts.size()).arg(soundfonts.size() == 1 ? "" : "s");
    status_ = QString("Scan: %1 CLAP, %2 VST3, %3 SoundFont, %4 failure%5")
                  .arg(clap.plugins.size()).arg(vst3.size())
                  .arg(soundfonts.size()).arg(clap.failures.size())
                  .arg(clap.failures.size() == 1 ? "" : "s");
    emit pluginsChanged();
    emit soundfontStatusChanged();
    emit statusChanged();
}

std::unique_ptr<blokkily::PluginInstance> AppController::createInstrument(
    const blokkily::InstrumentSlot& slot, std::string* error) const {
    if (slot.format == "CLAP")
        return blokkily::ClapPluginInstance::create(slot.path, slot.identifier, error);
    if (slot.format == "VST3") {
        std::size_t index = 0;
        const auto found = blokkily::Vst3PluginInstance::scan(slot.path);
        for (std::size_t candidate = 0; candidate < found.size(); ++candidate)
            if (found[candidate].identifier == slot.identifier) { index = candidate; break; }
        return blokkily::Vst3PluginInstance::create(slot.path, index, error);
    }
    if (slot.format == "SoundFont") {
        auto synth = std::make_unique<blokkily::SoundFontSynth>();
        if (synth->load(slot.path)) return synth;
        if (error != nullptr) *error = "could not load SoundFont";
        return nullptr;
    }
    if (error != nullptr) *error = "unknown instrument format";
    return nullptr;
}

void AppController::applyMix() {
    if (engine_ && song_ != nullptr) engine_->apply_mix(song_->song());
}

void AppController::pollMeters() {
    if (!engine_ || song_ == nullptr) return;
    if (transport_ != nullptr && transport_->playing())
        transport_->followSamples(engine_->sample_position(), engine_->sample_rate());
    std::vector<float> peaks(engine_->track_count(), 0.0F);
    for (std::size_t track = 0; track < peaks.size(); ++track)
        peaks[track] = engine_->track_peak(track);
    song_->setMeters(peaks, engine_->master_peak());
}

// An instrument is the same instrument when it is the same file loaded through
// the same format under the same identifier. What it holds inside changes as it
// is played, and that is not a reason to build the graph again.
namespace {
bool same_instrument(const blokkily::InstrumentSlot& left,
                     const blokkily::InstrumentSlot& right) {
    return left.format == right.format && left.path == right.path &&
           left.identifier == right.identifier;
}
} // namespace

bool AppController::builtFromCurrentInstruments() const {
    if (song_ == nullptr) return false;
    const auto& tracks = song_->song().tracks;
    if (tracks.size() != engine_slots_.size()) return false;
    for (std::size_t index = 0; index < tracks.size(); ++index)
        if (!same_instrument(tracks[index].instrument, engine_slots_[index])) return false;
    return true;
}

bool AppController::refreshArrangement() {
    if (!engine_ || song_ == nullptr || transport_ == nullptr) return false;
    if (!builtFromCurrentInstruments()) return false;
    std::string error;
    if (!engine_->recompile(song_->song(), transport_->bpm(), 0, &error)) {
        status_ = QString("Arrangement unchanged · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        return false;
    }
    transport_->setSongBars(song_->bars());
    return true;
}

bool AppController::auditionStep(int step) {
    if (pattern_ == nullptr) return false;
    const auto pitches = pattern_->pitchesAt(step);
    if (pitches.empty()) return false;
    return auditionPitches(pitches, 0.9);
}

// Banks that are General MIDI and are the ones a Linux, macOS, or Windows host
// is most likely to already have. The list is a preference, not a requirement:
// when none of them is installed the first SoundFont on the machine is used.
namespace {
constexpr const char* preferred_banks[] = {
    "FluidR3_GM.sf2",   "FluidR3_GM.sf3",   "GeneralUser.sf2",
    "GeneralUser GS.sf2", "default-GM.sf2", "default.sf2",
    "TimGM6mb.sf2",     "FatBoy.sf2",       "SGM-V2.01.sf2",
    "Arachno.sf2",      "gm.sf2",           "freepats-general-midi.sf2",
};

// What a track should be set to when nothing has said otherwise. A drum track
// wants the percussion bank rather than a grand piano, which is the difference
// between the opening song sounding like music and sounding like two pianos.
struct DefaultPreset {
    int bank = 0;
    int program = 0;
};

DefaultPreset preset_for_track(const std::string& name) {
    QString label = QString::fromStdString(name).toUpper();
    if (label.contains("DRUM") || label.contains("PERC") || label.contains("KIT"))
        return {128, 0};                      // the General MIDI percussion bank
    if (label.contains("BASS")) return {0, 33};   // electric bass, fingered
    if (label.contains("PAD") || label.contains("STRING")) return {0, 48};
    if (label.contains("LEAD") || label.contains("SYNTH")) return {0, 81};
    return {0, 0};                            // acoustic grand
}
} // namespace

bool AppController::loadDefaultInstrument() {
    return loadDefaultInstrument(blokkily::SoundFontCatalog::system_paths());
}

bool AppController::loadDefaultInstrument(const std::vector<std::filesystem::path>& roots) {
    if (song_ == nullptr) return false;
    auto& song = song_->song();
    const bool anything_loaded =
        std::any_of(song.tracks.begin(), song.tracks.end(),
                    [](const blokkily::Track& track) { return !track.instrument.format.empty(); });
    if (anything_loaded) return false;

    std::filesystem::path bank;
    for (const char* wanted : preferred_banks) {
        for (const auto& root : roots) {
            std::error_code ignored;
            const auto candidate = root / wanted;
            if (std::filesystem::is_regular_file(candidate, ignored)) { bank = candidate; break; }
        }
        if (!bank.empty()) break;
    }
    // No familiar bank installed, so whatever this machine does have will do.
    if (bank.empty()) {
        const auto found = blokkily::SoundFontCatalog::scan_paths(roots);
        if (found.empty()) {
            soundfont_status_ = "No SoundFont installed — load a plugin to hear the song";
            emit soundfontStatusChanged();
            return false;
        }
        bank = found.front();
    }

    for (std::size_t track = 0; track < song.tracks.size(); ++track) {
        const auto preset = preset_for_track(song.tracks[track].name);
        blokkily::InstrumentSlot slot;
        slot.format = "SoundFont";
        slot.path = bank.string();
        // The preset travels as the instrument's own state, which is the same
        // road a saved project takes, so an opening session and a reloaded one
        // reach the synth through one path.
        const std::string state = slot.path + '\n' + std::to_string(preset.bank) + '\n' +
                                  std::to_string(preset.program);
        slot.state.resize(state.size());
        std::memcpy(slot.state.data(), state.data(), state.size());
        song.tracks[track].instrument = slot;
    }
    song_->refreshStructure();
    soundfont_status_ = QString("%1 · %2 track%3")
                            .arg(QString::fromStdString(bank.stem().string()))
                            .arg(song.tracks.size())
                            .arg(song.tracks.size() == 1 ? "" : "s");
    emit soundfontStatusChanged();
    return engine_ != nullptr;
}

bool AppController::rebuildEngine() {
    if (song_ == nullptr || transport_ == nullptr) return false;
    const bool resume = transport_->playing();
    auto& song = song_->song();

    // State streams belong to the control thread while the processor is idle.
    if (audio_output_) audio_output_->stop();

    // Carry each instrument's own state across the rebuild, so recompiling the
    // arrangement never resets a synth a producer has already dialled in.
    if (engine_)
        for (std::size_t track = 0; track < song.tracks.size(); ++track)
            if (engine_->has_instrument(track) && track < engine_slots_.size() &&
                same_instrument(song.tracks[track].instrument, engine_slots_[track])) {
                auto state = engine_->save_track_state(track);
                if (!state.empty()) song.tracks[track].instrument.state = std::move(state);
            }
    forgetSoundingNotes();
    engine_.reset();

    auto next = std::make_unique<blokkily::SongEngine>();
    std::string error;
    int loaded = 0;
    for (std::size_t track = 0; track < song.tracks.size(); ++track) {
        const auto& slot = song.tracks[track].instrument;
        if (slot.format.empty()) continue;
        std::string reason;
        auto instrument = createInstrument(slot, &reason);
        if (!instrument) { if (error.empty()) error = reason; continue; }
        next->set_instrument(track, std::move(instrument));
        ++loaded;
    }
    if (!next->prepare(song, transport_->bpm(), 48000.0, 512, 0, &error)) {
        status_ = QString("Song could not prepare · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        emit activeInstrumentChanged();
        return false;
    }
    for (std::size_t track = 0; track < song.tracks.size(); ++track) {
        const auto& state = song.tracks[track].instrument.state;
        if (!state.empty()) (void)next->load_track_state(track, state);
    }
    engine_ = std::move(next);
    engine_slots_.clear();
    engine_slots_.reserve(song.tracks.size());
    for (const auto& track : song.tracks) engine_slots_.push_back(track.instrument);
    transport_->setSongBars(song_->bars());

    // The engine exists even when the machine has no audio device, so a song
    // can still be arranged and bounced to a file on a silent host.
    if (!audio_output_ || !audio_output_->is_open()) {
        auto output = audio_output_ ? std::move(audio_output_)
                                   : std::make_unique<blokkily::RtAudioOutput>();
        std::string device_error;
        if (output->open(*engine_, 48000, 512, &device_error)) {
            audio_output_ = std::move(output);
        } else {
            status_ = QString("Audio device unavailable · %1")
                          .arg(QString::fromStdString(device_error));
            emit statusChanged();
            emit activeInstrumentChanged();
            return loaded > 0;
        }
    } else {
        audio_output_->rebind(*engine_);
    }
    engine_->set_playing(resume);
    if (resume && !audio_output_->start(&error)) {
        transport_->stop();
        engine_->set_playing(false);
        status_ = QString("Audio start failed · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        return false;
    }
    status_ = QString("Ready · %1 track%2, %3 instrument%4")
                  .arg(song.tracks.size()).arg(song.tracks.size() == 1 ? "" : "s")
                  .arg(loaded).arg(loaded == 1 ? "" : "s");
    emit statusChanged();
    emit activeInstrumentChanged();
    return true;
}

void AppController::assignInstrument(int track, const blokkily::InstrumentSlot& slot,
                                     const QString& label) {
    if (song_ == nullptr) return;
    while (song_->trackCount() <= track) song_->addTrack();
    song_->song().tracks[static_cast<std::size_t>(track)].name = label.toStdString();
    song_->setInstrument(track, slot);
}

QVariantList AppController::browserPlugins() const {
    const auto query = browser_filter_.trimmed().toStdString();
    // A ranked row and where it came from, kept together so the browser can
    // load what it lists.
    struct Ranked {
        int score;
        int source;
    };
    std::vector<Ranked> ranked;
    ranked.reserve(static_cast<std::size_t>(plugins_.size()));
    for (int index = 0; index < plugins_.size(); ++index) {
        const auto fields = plugins_.at(index).toMap();
        // Name, maker and format are all searched, so an instrument is found
        // by who made it or by what kind of plugin it is as readily as by its
        // own name.
        const auto haystack = QString("%1 %2 %3")
                                  .arg(fields.value("name").toString(),
                                       fields.value("vendor").toString(),
                                       fields.value("format").toString())
                                  .toStdString();
        const int score = blokkily::browser_match_score(query, haystack);
        if (score >= 0) ranked.push_back({score, index});
    }
    // Closest match first; entries the query cannot separate keep the order
    // the scan found them in, so an unfiltered browser is the plain list.
    std::stable_sort(ranked.begin(), ranked.end(),
                     [](const Ranked& left, const Ranked& right) {
                         return left.score > right.score;
                     });
    QVariantList listed;
    listed.reserve(static_cast<qsizetype>(ranked.size()));
    for (const auto& entry : ranked) {
        auto fields = plugins_.at(entry.source).toMap();
        fields.insert("source", entry.source);
        listed.push_back(fields);
    }
    return listed;
}

void AppController::setBrowserFilter(const QString& query) {
    if (browser_filter_ == query) return;
    browser_filter_ = query;
    emit browserChanged();
}

bool AppController::selectInstrument(int index) {
    if (song_ == nullptr || index < 0 || index >= plugins_.size()) return false;
    const auto entry = plugins_.at(index).toMap();
    const QString format = entry.value("format").toString();
    blokkily::InstrumentSlot slot;
    slot.format = (format == "SF" ? QStringLiteral("SoundFont") : format).toStdString();
    slot.path = entry.value("path").toString().toStdString();
    slot.identifier = entry.value("identifier").toString().toStdString();
    song_->setInstrument(song_->selectedTrack(), slot);
    return engine_ != nullptr;
}

void AppController::togglePlayback() {
    // Pressing Play is a request to hear the song. A session that has not been
    // given an instrument yet gets the default bank here rather than being told
    // to go and find one.
    if (!engine_ && transport_ != nullptr) (void)loadDefaultInstrument();
    if (!engine_ || !transport_) {
        status_ = "No instrument could be loaded — pick one from the plugin browser";
        emit statusChanged();
        return;
    }
    if (transport_->playing()) {
        engine_->set_playing(false);
        // Keep the callback alive for note-offs, instrument releases, meters,
        // and the next key played on the stopped transport.
        transport_->stop();
        pollMeters();
        return;
    }
    std::string error;
    engine_->set_playing(true);
    if (audio_output_ && !audio_output_->start(&error)) {
        engine_->set_playing(false);
        status_ = QString("Audio start failed · %1").arg(QString::fromStdString(error));
        emit statusChanged();
        return;
    }
    meter_timer_.start();
    transport_->followSamples(engine_->sample_position(), engine_->sample_rate());
    transport_->play();
}

void AppController::rewindPlayback() {
    if (engine_) engine_->rewind();
    if (transport_) transport_->rewind();
}

void AppController::setTempo(double bpm) {
    if (!transport_) return;
    transport_->setBpm(bpm);
    // The tempo readout is dragged, so this arrives once per pointer move. A
    // tempo change is a change to when the arrangement's events fall, not to
    // the instruments playing them, so it goes to the running engine.
    if (engine_) (void)refreshArrangement();
}

bool AppController::exportAudioFile(const QString& path, const QString& depth) {
    if (!engine_) {
        export_status_ = "Load an instrument before exporting";
        emit exportStatusChanged();
        return false;
    }
    const auto format = depth == "PCM16"  ? blokkily::WaveFormat::pcm16
                      : depth == "PCM24"  ? blokkily::WaveFormat::pcm24
                                          : blokkily::WaveFormat::float32;
    std::string error;
    const bool resume_device = audio_output_ && audio_output_->is_running();
    if (audio_output_) audio_output_->stop();
    releaseSoundingNotes();
    // Half a second of tail so the last note's release is part of the file.
    const auto report = bounce_song(*engine_, local_path(path).toStdString(), format,
                                    24000, &error);
    if (resume_device) {
        std::string resume_error;
        if (!audio_output_->start(&resume_error)) {
            engine_->set_playing(false);
            if (transport_) transport_->stop();
            status_ = QString("Audio restart failed · %1").arg(QString::fromStdString(resume_error));
            emit statusChanged();
        }
    }
    if (!report) {
        export_status_ = QString("Export failed · %1").arg(QString::fromStdString(error));
        emit exportStatusChanged();
        return false;
    }
    export_status_ = QString("%1 · %2 s · peak %3 dB%4")
                         .arg(QFileInfo(local_path(path)).fileName())
                         .arg(static_cast<double>(report->frames) / 48000.0, 0, 'f', 1)
                         .arg(blokkily::linear_to_db(report->peak), 0, 'f', 1)
                         .arg(report->clipped ? " · CLIPPED" : "");
    emit exportStatusChanged();
    return true;
}

bool AppController::saveProjectFile(const QString& path) {
    return saveProject(local_path(path));
}

bool AppController::loadProjectFile(const QString& path) {
    const bool loaded = loadProject(local_path(path));
    if (loaded && transport_) transport_->rewind();
    return loaded;
}

bool AppController::scanClapFile(const QString& path) {
    const auto result = blokkily::ClapCatalog{}.scan_file(path.toStdString());
    plugins_.clear();
    for (const auto& plugin : result.plugins)
        plugins_.push_back(plugin_entry("CLAP", plugin.name, plugin.vendor));
    status_ = QString("CLAP fixture: %1 plugin%2, %3 failure%4")
                  .arg(result.plugins.size()).arg(result.plugins.size() == 1 ? "" : "s")
                  .arg(result.failures.size()).arg(result.failures.size() == 1 ? "" : "s");
    emit pluginsChanged();
    emit statusChanged();
    return result.plugins.size() == 1 && result.failures.empty();
}

bool AppController::verifyClap(const QString& path) {
    if (!scanClapFile(path) || plugins_.isEmpty()) return false;
    std::string error;
    auto plugin = blokkily::ClapPluginInstance::create(
        path.toStdString(), "dev.blokkily.test", &error);
    bool valid = plugin != nullptr && plugin->activate(48000.0, 1, 256);
    std::vector<float> left(128), right(128);
    if (valid) {
        const blokkily::PluginEvent note{blokkily::PluginEvent::Type::note_on, 64, 60, 1.0};
        plugin->process({left, right}, std::span{&note, 1});
        valid = left[63] == 0.0F && left[64] != 0.0F;
        const auto state = plugin->save_state();
        valid = valid && !state.empty() && plugin->load_state(state);
    }
    if (valid) {
        auto transport_plugin = blokkily::ClapPluginInstance::create(
            path.toStdString(), "dev.blokkily.test", &error);
        blokkily::RealtimePlayback playback(std::move(transport_plugin));
        blokkily::Pattern pattern(1920, 480);
        blokkily::Trigger trigger;
        trigger.start = 120;
        trigger.duration = 120;
        trigger.musical_data = blokkily::Note{60, 1.0F, 0.0F};
        (void)pattern.add(trigger);
        valid = playback.prepare(pattern, 120.0, 48000.0, 8192);
        std::vector<float> transport_left(8192), transport_right(8192);
        playback.set_playing(true);
        playback.process({transport_left, transport_right});
        valid = valid && transport_left[5999] == 0.0F && transport_left[6000] != 0.0F;
    }
    if (valid) {
        auto slot_plugin = blokkily::ClapPluginInstance::create(
            path.toStdString(), "dev.blokkily.test", &error);
        if (slot_plugin != nullptr)
            assignInstrument(0, {"CLAP", path.toStdString(), "dev.blokkily.test",
                                 slot_plugin->save_state()}, "CLAP LEAD");
    }
    status_ = valid ? "CLAP → transport: sample-accurate"
                    : QString("CLAP processing failed · %1").arg(QString::fromStdString(error));
    emit statusChanged();
    return valid;
}

bool AppController::verifyVst3(const QString& path) {
    const auto descriptors = blokkily::Vst3PluginInstance::scan(path.toStdString());
    bool valid = descriptors.size() == 1;
    std::string error;
    if (valid) {
        auto plugin = blokkily::Vst3PluginInstance::create(path.toStdString(), 0, &error);
        valid = plugin != nullptr && plugin->activate(48000.0, 1, 256);
        if (valid) {
            std::vector<float> left(128), right(128);
            const blokkily::PluginEvent note{blokkily::PluginEvent::Type::note_on, 32, 60, 1.0};
            plugin->process({left, right}, std::span{&note, 1});
            valid = left[31] == 0.0F && left[32] != 0.0F;
            const auto state = plugin->save_state();
            valid = valid && !state.empty() && plugin->load_state(state);
        }
    }
    if (valid) {
        plugins_.push_back(plugin_entry("VST3", descriptors.front().name,
                                        descriptors.front().manufacturer));
        auto slot_plugin = blokkily::Vst3PluginInstance::create(path.toStdString(), 0, &error);
        if (slot_plugin != nullptr)
            assignInstrument(1, {"VST3", path.toStdString(), descriptors.front().identifier,
                                 slot_plugin->save_state()}, "VST3 PAD");
    }
    status_ = valid
        ? QString("CLAP + VST3 → transport: sample-accurate")
        : QString("VST3 processing failed · %1").arg(QString::fromStdString(error));
    emit pluginsChanged();
    emit statusChanged();
    return valid;
}

// Proves through the whole app that a parameter lock on a step actually
// changes what that step sounds like, rather than only being stored.
bool AppController::verifyParameterLocks(const QString& clap_path) {
    std::string error;
    auto plugin = blokkily::ClapPluginInstance::create(
        clap_path.toStdString(), "dev.blokkily.test", &error);
    if (plugin == nullptr) {
        status_ = "Parameter lock check could not create the CLAP instrument";
        emit statusChanged();
        return false;
    }
    blokkily::Pattern pattern(1920, 480);
    blokkily::Trigger trigger;
    trigger.start = 120;
    trigger.duration = 120;
    trigger.musical_data = blokkily::Note{60, 1.0F, 0.0F};
    trigger.locks = {{"level", 0, 0.75, blokkily::ParameterLock::Kind::automation},
                     {"level", 0, 0.125, blokkily::ParameterLock::Kind::modulation}};
    (void)pattern.add(trigger);

    blokkily::RealtimePlayback playback(std::move(plugin));
    bool valid = playback.prepare(pattern, 120.0, 48000.0, 8192);
    std::vector<float> left(8192), right(8192);
    playback.set_playing(true);
    playback.process({left, right});
    valid = valid && left[5999] == 0.0F && left[6000] > 0.874F && left[6000] < 0.876F;
    status_ = valid ? "Parameter locks → automation + modulation applied"
                    : "Parameter locks did not reach the instrument";
    emit statusChanged();
    return valid;
}

bool AppController::verifySoundFont(const QString& path) {
    blokkily::SoundFontSynth synth;
    bool valid = synth.load(path.toStdString()) && synth.activate(48000.0, 1, 2048);
    std::vector<float> left(2048), right(2048);
    if (valid) {
        const blokkily::PluginEvent note{blokkily::PluginEvent::Type::note_on, 0, 60, 1.0};
        synth.process({left, right}, std::span{&note, 1});
        double energy = 0.0;
        for (const float sample : left) energy += std::abs(sample);
        valid = energy > 0.001;
    }
    if (valid)
        assignInstrument(2, {"SoundFont", path.toStdString(), "", synth.save_state()},
                         "SF KEYS");
    soundfont_status_ = valid
        ? QString("Rendered audio · %1").arg(QFileInfo(path).fileName())
        : QString("SoundFont failed · %1").arg(QFileInfo(path).fileName());
    emit soundfontStatusChanged();
    return valid;
}

// The mixer must move real audio, not just numbers on a strip: mute has to
// remove a track from the bus and solo has to leave only its own.
bool AppController::verifyMixer() {
    if (song_ == nullptr || !engine_) return false;
    auto& song = song_->song();
    if (song.tracks.size() < 2) return false;
    const std::vector<blokkily::MixerStrip> before{song.tracks[0].mix, song.tracks[1].mix};
    const bool was_playing = engine_->is_playing();
    const auto resume_at = engine_->sample_position();

    std::vector<float> left(512), right(512);
    const auto render_at = [&](std::uint64_t position) {
        engine_->apply_mix(song);
        engine_->seek(position);
        engine_->set_playing(true);
        std::fill(left.begin(), left.end(), 0.0F);
        std::fill(right.begin(), right.end(), 0.0F);
        engine_->process({left, right});
    };

    song.tracks[0].mix = {0.0, 0.0, false, false};
    song.tracks[1].mix = {0.0, 0.0, false, false};

    // Tracks do not all play from bar one, so find a moment where the track
    // under test actually sounds and judge mute and solo at that same moment.
    std::uint64_t sounding_at = 0;
    bool sounds = false;
    const auto blocks = engine_->song_samples() / 512 + 1;
    for (std::uint64_t block = 0; block < blocks && !sounds; ++block) {
        render_at(block * 512);
        sounds = engine_->track_peak(1) > 0.0F;
        if (sounds) sounding_at = block * 512;
    }

    render_at(sounding_at);
    const float open_peak = engine_->track_peak(1);
    song.tracks[1].mix.mute = true;
    render_at(sounding_at);
    const float muted_peak = engine_->track_peak(1);
    song.tracks[1].mix.mute = false;
    song.tracks[0].mix.solo = true;
    render_at(sounding_at);
    const float shadowed_peak = engine_->track_peak(1);

    song.tracks[0].mix = before[0];
    song.tracks[1].mix = before[1];
    engine_->apply_mix(song);
    engine_->set_playing(was_playing);
    engine_->seek(resume_at);

    const bool valid = sounds && open_peak > 0.0F && muted_peak == 0.0F && shadowed_peak == 0.0F;
    status_ = valid ? "Mixer → mute and solo change the bus"
                    : "Mixer did not change what the bus carries";
    emit statusChanged();
    return valid;
}

// The export gate: a bounce must be a real, readable, non-silent stereo file of
// the arrangement's length, not an empty file that merely exists.
bool AppController::verifyBounce(const QString& path) {
    if (!engine_) return false;
    if (!exportAudioFile(path, "PCM24")) return false;
    std::string error;
    const auto rendered = blokkily::read_wave(local_path(path).toStdString(), &error);
    if (!rendered) {
        export_status_ = QString("Export unreadable · %1").arg(QString::fromStdString(error));
        emit exportStatusChanged();
        return false;
    }
    const bool valid = rendered->channels == 2 && rendered->sample_rate == 48000 &&
                       rendered->frames == engine_->song_samples() + 24000 &&
                       std::any_of(rendered->interleaved.begin(), rendered->interleaved.end(),
                                   [](float sample) { return std::abs(sample) > 0.0001F; });
    if (!valid) {
        export_status_ = "Export did not contain the arrangement";
        emit exportStatusChanged();
    }
    return valid;
}

bool AppController::saveProject(const QString& path) {
    if (song_ == nullptr) return false;
    auto& song = song_->song();
    // Ask every live instrument what it wants persisted before writing.
    if (engine_)
        for (std::size_t track = 0; track < song.tracks.size(); ++track)
            if (engine_->has_instrument(track)) {
                auto state = engine_->save_track_state(track);
                if (!state.empty()) song.tracks[track].instrument.state = std::move(state);
            }
    blokkily::Project project;
    project.name = "Blokkily Session";
    project.tempo = transport_ == nullptr ? 120.0 : transport_->bpm();
    project.song = song;
    std::string error;
    const bool valid = blokkily::ProjectFile::save(project, path.toStdString(), &error);
    project_status_ = valid ? "Saved" : "Save failed";
    project_detail_ = valid ? QString("%1 · %2 track%3")
                                  .arg(QFileInfo(path).fileName())
                                  .arg(song.tracks.size())
                                  .arg(song.tracks.size() == 1 ? "" : "s")
                            : QString::fromStdString(error);
    emit projectStatusChanged();
    return valid;
}

bool AppController::loadProject(const QString& path) {
    if (song_ == nullptr) return false;
    std::string error;
    auto project = blokkily::ProjectFile::load(path.toStdString(), &error);
    if (!project) {
        project_status_ = "Load failed";
        project_detail_ = QString::fromStdString(error);
        emit projectStatusChanged();
        return false;
    }
    if (audio_output_) audio_output_->stop();
    forgetSoundingNotes();
    engine_.reset();
    if (transport_ != nullptr) transport_->setBpm(project->tempo);
    const auto tracks = project->song.tracks.size();
    const auto patterns = project->song.patterns.size();
    song_->replace(std::move(project->song));
    if (pattern_ != nullptr) pattern_->refresh();
    project_status_ = QString("Restored from disk");
    project_detail_ = QString("%1 pattern%2 · %3 track%4")
                          .arg(patterns).arg(patterns == 1 ? "" : "s")
                          .arg(tracks).arg(tracks == 1 ? "" : "s");
    emit projectStatusChanged();
    (void)rebuildEngine();
    return true;
}
