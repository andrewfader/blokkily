#include "pattern_model.hpp"

#include <QString>
#include <QVariantMap>

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <vector>

namespace {
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

QVariantList voice_keys(const blokkily::Trigger& trigger) {
    QVariantList keys;
    if (const auto* note = std::get_if<blokkily::Note>(&trigger.musical_data)) {
        keys.push_back(static_cast<int>(note->key));
        return keys;
    }
    const auto& chord = std::get<blokkily::Chord>(trigger.musical_data);
    for (const auto interval : chord.intervals)
        keys.push_back(qBound(0, chord.root + interval, 127));
    return keys;
}

void transpose_trigger(blokkily::Trigger& trigger, int semitones) {
    if (auto* note = std::get_if<blokkily::Note>(&trigger.musical_data)) {
        note->key = static_cast<std::int16_t>(qBound(0, note->key + semitones, 127));
        return;
    }
    auto& chord = std::get<blokkily::Chord>(trigger.musical_data);
    chord.root = static_cast<std::int16_t>(qBound(0, chord.root + semitones, 127));
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
    case VoiceKeysRole: return voice_keys(event);
    default: return {};
    }
}

QHash<int, QByteArray> PatternModel::roleNames() const {
    return {{IdRole, "eventId"}, {StepRole, "step"}, {KeyRole, "key"},
            {NameRole, "noteName"}, {DurationRole, "duration"},
            {VelocityRole, "velocityHex"}, {LockRole, "lockText"},
            {VoiceKeysRole, "voiceKeys"}};
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
            row["duration"] = 0;
            row["voiceKeys"] = QVariantList{};
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
            row["duration"] = static_cast<int>(trigger->duration);
            row["voiceKeys"] = voice_keys(*trigger);
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
    detail["duration"] = static_cast<int>(trigger->duration);
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

namespace {
// The lowest and highest key any voice of the pattern sounds. A chord is one
// event, but each of its voices is drawn on its own lane.
std::pair<int, int> key_range(const blokkily::Pattern& pattern) {
    int lowest = 127;
    int highest = 0;
    for (const auto& event : pattern.events()) {
        if (const auto* note = std::get_if<blokkily::Note>(&event.musical_data)) {
            lowest = std::min(lowest, static_cast<int>(note->key));
            highest = std::max(highest, static_cast<int>(note->key));
            continue;
        }
        const auto& chord = std::get<blokkily::Chord>(event.musical_data);
        for (const auto interval : chord.intervals) {
            const int key = qBound(0, chord.root + interval, 127);
            lowest = std::min(lowest, key);
            highest = std::max(highest, key);
        }
    }
    if (lowest > highest) return {48, 60};
    return {lowest, highest};
}
} // namespace

int PatternModel::lowKey() const {
    const auto [lowest, highest] = key_range(pattern());
    int low = lowest - 4;
    const int span = (highest + 4) - low;
    if (span < minimum_roll_span) low -= (minimum_roll_span - span) / 2;
    return qBound(0, low, 127 - minimum_roll_span);
}

int PatternModel::highKey() const {
    const auto highest = key_range(pattern()).second;
    return qBound(lowKey() + minimum_roll_span, highest + 4, 127);
}

bool PatternModel::hasStep(int step) const { return triggerAt(step) != nullptr; }

int PatternModel::stepDuration(int step) const {
    const auto* trigger = triggerAt(step);
    return trigger == nullptr ? 0 : static_cast<int>(trigger->duration);
}

int PatternModel::stepKey(int step) const {
    const auto* trigger = triggerAt(step);
    return trigger == nullptr ? 0 : pitch_key(*trigger);
}

void PatternModel::selectStep(int step) {
    // Selection is not an edit: the arrangement still plays what it played, so
    // nothing here may reach the engine and interrupt it.
    selected_step_ = qBound(-1, step, step_count - 1);
    emit selectionChanged();
}

void PatternModel::toggleStep(int step, int key) {
    if (step < 0 || step >= step_count) return;
    song_->checkpoint();
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
    song_->checkpoint();
    const auto id = existing->id;
    beginResetModel();
    (void)pattern().remove(id);
    endResetModel();
    selected_step_ = step;
    emit patternChanged();
    emit contentChanged();
    emit selectionChanged();
}

void PatternModel::setStepDuration(int step, int ticks) {
    const auto bounded = qBound(24, ticks, 1920);
    const auto* existing = triggerAt(step);
    if (existing == nullptr || existing->duration == bounded) return;
    mutate(step, [bounded](blokkily::Trigger& trigger) { trigger.duration = bounded; },
           QStringLiteral("duration"));
}

void PatternModel::setSelectedDuration(int ticks) { setStepDuration(selected_step_, ticks); }

void PatternModel::setStepVelocity(int step, double velocity) {
    selectStep(step);
    setSelectedVelocity(velocity);
}

void PatternModel::relocateStep(int from, int to, int semitones) {
    if (from < 0 || from >= step_count || to < 0 || to >= step_count) return;
    const auto* existing = triggerAt(from);
    if (existing == nullptr) return;
    if (from == to && semitones == 0) return;
    auto moved = *existing;
    moved.start = to * ticks_per_step;
    transpose_trigger(moved, semitones);
    const auto from_id = existing->id;
    blokkily::EventId occupant_id = 0;
    if (from != to)
        if (const auto* occupant = triggerAt(to)) occupant_id = occupant->id;
    song_->checkpoint();
    beginResetModel();
    if (from != to) {
        (void)pattern().remove(from_id);
        if (occupant_id != 0) (void)pattern().remove(occupant_id);
        (void)pattern().add(moved);
    } else {
        (void)pattern().update(from_id, moved);
    }
    endResetModel();
    selected_step_ = to;
    emit patternChanged();
    emit contentChanged();
    emit selectionChanged();
}

void PatternModel::copySelected() {
    const auto* existing = selected_step_ < 0 ? nullptr : triggerAt(selected_step_);
    has_clipboard_ = true;
    if (existing != nullptr) clipboard_ = *existing;
    else clipboard_.reset();
}

bool PatternModel::pasteSelected() {
    if (!has_clipboard_ || selected_step_ < 0 || selected_step_ >= step_count) return false;
    if (!clipboard_) {
        if (triggerAt(selected_step_) == nullptr) return true;
        clearStep(selected_step_);
        return true;
    }
    song_->checkpoint();
    beginResetModel();
    if (const auto* existing = triggerAt(selected_step_)) (void)pattern().remove(existing->id);
    auto placed = *clipboard_;
    placed.start = selected_step_ * ticks_per_step;
    (void)pattern().add(placed);
    endResetModel();
    emit patternChanged();
    emit contentChanged();
    emit selectionChanged();
    return true;
}

bool PatternModel::duplicateSelected() {
    if (selected_step_ < 0 || selected_step_ >= step_count - 1) return false;
    if (triggerAt(selected_step_) == nullptr) return false;
    copySelected();
    selectStep(selected_step_ + 1);
    return pasteSelected();
}

bool PatternModel::insertStep(int step) {
    if (step < 0 || step >= step_count) return false;
    std::vector<std::optional<blokkily::Trigger>> held(static_cast<std::size_t>(step_count));
    for (int index = 0; index < step_count; ++index)
        if (const auto* existing = triggerAt(index))
            held[static_cast<std::size_t>(index)] = *existing;

    song_->checkpoint();
    beginResetModel();
    const auto living = pattern().events();
    std::vector<blokkily::EventId> ids;
    ids.reserve(living.size());
    for (const auto& event : living) ids.push_back(event.id);
    for (const auto id : ids) (void)pattern().remove(id);

    for (int index = 0; index < step; ++index) {
        if (!held[static_cast<std::size_t>(index)]) continue;
        auto kept = *held[static_cast<std::size_t>(index)];
        kept.start = index * ticks_per_step;
        (void)pattern().add(kept);
    }
    // Rows at and after the cursor move down one; the last row falls off.
    for (int index = step; index < step_count - 1; ++index) {
        if (!held[static_cast<std::size_t>(index)]) continue;
        auto moved = *held[static_cast<std::size_t>(index)];
        moved.start = (index + 1) * ticks_per_step;
        (void)pattern().add(moved);
    }
    endResetModel();
    selected_step_ = step;
    emit patternChanged();
    emit contentChanged();
    emit selectionChanged();
    return true;
}

bool PatternModel::deleteAndShift(int step) {
    if (step < 0 || step >= step_count) return false;
    std::vector<std::optional<blokkily::Trigger>> held(static_cast<std::size_t>(step_count));
    for (int index = 0; index < step_count; ++index)
        if (const auto* existing = triggerAt(index))
            held[static_cast<std::size_t>(index)] = *existing;

    song_->checkpoint();
    beginResetModel();
    const auto living = pattern().events();
    std::vector<blokkily::EventId> ids;
    ids.reserve(living.size());
    for (const auto& event : living) ids.push_back(event.id);
    for (const auto id : ids) (void)pattern().remove(id);

    for (int index = 0; index < step; ++index) {
        if (!held[static_cast<std::size_t>(index)]) continue;
        auto kept = *held[static_cast<std::size_t>(index)];
        kept.start = index * ticks_per_step;
        (void)pattern().add(kept);
    }
    // Later rows pull up into the hole; the last row becomes empty.
    for (int index = step + 1; index < step_count; ++index) {
        if (!held[static_cast<std::size_t>(index)]) continue;
        auto moved = *held[static_cast<std::size_t>(index)];
        moved.start = (index - 1) * ticks_per_step;
        (void)pattern().add(moved);
    }
    endResetModel();
    selected_step_ = step;
    emit patternChanged();
    emit contentChanged();
    emit selectionChanged();
    return true;
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
    if (const auto* existing = triggerAt(step);
        existing != nullptr && std::holds_alternative<blokkily::Note>(existing->musical_data)) {
        const auto& held = std::get<blokkily::Note>(existing->musical_data);
        // Writing the pitch a step already has is not an edit, and a drag that
        // passes back over its starting lane must not fill history with them.
        if (held.key == note.key && held.cents == note.cents && held.velocity == note.velocity) {
            selected_step_ = step;
            emit selectionChanged();
            return;
        }
    }
    song_->checkpoint();
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

void PatternModel::notifyRecorded() {
    refresh();
    emit contentChanged();
}

void PatternModel::placeChord(int step, const blokkily::Chord& chord) {
    if (step < 0 || step >= step_count) return;
    auto trigger = played_trigger(step, ticks_per_step);
    trigger.musical_data = chord;
    song_->checkpoint();
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

void PatternModel::mutate(int step, const std::function<void(blokkily::Trigger&)>& edit,
                          const QString& merge) {
    const auto* existing = triggerAt(step);
    if (existing == nullptr) return;
    blokkily::Trigger replacement = *existing;
    edit(replacement);
    song_->checkpoint(merge.isEmpty() ? QString() : QString("%1:%2").arg(merge).arg(step));
    beginResetModel();
    (void)pattern().update(replacement.id, replacement);
    endResetModel();
    emit patternChanged();
    emit contentChanged();
    emit selectionChanged();
}

void PatternModel::transposeSelected(int semitones) {
    mutate(selected_step_, [semitones](blokkily::Trigger& trigger) {
        transpose_trigger(trigger, semitones);
    });
}

void PatternModel::setSelectedVelocity(double velocity) {
    mutate(selected_step_, [velocity](blokkily::Trigger& trigger) {
        if (auto* note = std::get_if<blokkily::Note>(&trigger.musical_data))
            note->velocity = static_cast<float>(qBound(0.0, velocity, 1.0));
    }, QStringLiteral("velocity"));
}

void PatternModel::setSelectedProbability(double probability) {
    mutate(selected_step_, [probability](blokkily::Trigger& trigger) {
        trigger.probability = static_cast<float>(qBound(0.0, probability, 1.0));
    }, QStringLiteral("probability"));
}

void PatternModel::setSelectedRatchets(int ratchets) {
    mutate(selected_step_, [ratchets](blokkily::Trigger& trigger) {
        trigger.ratchets = static_cast<std::uint8_t>(qBound(1, ratchets, 16));
    });
}

void PatternModel::setSelectedMicroOffset(int ticks) {
    mutate(selected_step_, [ticks](blokkily::Trigger& trigger) {
        trigger.micro_offset = qBound(-59, ticks, 59);
    }, QStringLiteral("micro"));
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
    }, QStringLiteral("lock"));
}

void PatternModel::clearSelectedLock() {
    mutate(selected_step_, [](blokkily::Trigger& trigger) { trigger.locks.clear(); });
}

void PatternModel::replace(blokkily::Pattern replacement) {
    song_->checkpoint();
    beginResetModel();
    pattern() = std::move(replacement);
    endResetModel();
    emit patternChanged();
    emit contentChanged();
    emit selectionChanged();
}
