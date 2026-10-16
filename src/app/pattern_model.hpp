#pragma once

#include "song_model.hpp"

#include "blokkily/model/pattern.hpp"
#include "blokkily/guide/event_bus.hpp"

#include <QAbstractListModel>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <optional>
#include <vector>

// The canonical pattern, projected for every editor at once. The step grid, the
// tracker, and the piano roll all read from this one object; `steps` is the
// shared row projection the grid and the tracker each render differently.
class PatternModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int eventCount READ rowCount NOTIFY patternChanged)
    Q_PROPERTY(QVariantList steps READ steps NOTIFY patternChanged)
    // The pattern's controller movements (wave 4.1): one map per movement
    // with its tick, kind ("bend", "cc", "pressure" or "poly"), controller
    // (the CC number, or the key for poly pressure) and a level (-1..1 for
    // the wheel, 0..1 otherwise). The piano roll's controller lane draws and
    // edits them through drawControl, moveControl and eraseControls.
    Q_PROPERTY(QVariantList controls READ controls NOTIFY patternChanged)
    // The selected note's own expression (wave 4.1, MPE): one map per point
    // with its tick in the pattern (the note's start plus its offset), its
    // offset, voice, kind ("notepitch", "notetimbre", "notepressure") and
    // level (pitch -1..1 for -12..+12 semitones, the others 0..1). The
    // controller lane edits it through drawControl, moveControl and
    // eraseControls with those kinds and the voice as the controller; "poly"
    // with a key edits that key's poly pressure in the pattern.
    Q_PROPERTY(QVariantList noteExpression READ noteExpression NOTIFY selectedChanged)
    // The open pattern's length in ticks, for a lane that maps x to ticks.
    Q_PROPERTY(int patternTicks READ patternTicks NOTIFY patternChanged)
    // Where the cursor is, kept apart from what the pattern holds: moving the
    // cursor is not an edit, and nothing downstream may treat it as one.
    Q_PROPERTY(int selectedStep READ selectedStep NOTIFY selectionChanged)
    // What the cursor's step holds. Read again whenever the cursor moves and
    // whenever the pattern under it is, whoever changed it: an undo, the
    // assistant or an agent edits the step without moving the cursor.
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectedChanged)
    // Pitch window the piano roll draws, widened to whatever the pattern uses
    // until the producer scrolls it (scrollRoll), which moves it anywhere in
    // the MIDI range, so no pitch is out of the roll's reach.
    Q_PROPERTY(int lowKey READ lowKey NOTIFY patternChanged)
    Q_PROPERTY(int highKey READ highKey NOTIFY patternChanged)
    // How many sixteenth steps the open pattern has: sixteen for a 4/4 bar,
    // fourteen for a 7/8 one, or whatever its LEN was set to. The grid, the
    // tracker and the roll each draw this many columns.
    Q_PROPERTY(int stepCount READ stepCount NOTIFY patternChanged)
    // The stem rack: every track's part of the open section, one row each,
    // as {track, name, selected, steps: [bool per step]}, so the whole
    // pattern is seen at once and any stem's steps are a click away.
    Q_PROPERTY(QVariantList stemRows READ stemRows NOTIFY patternChanged)
    // The tracker's channels: every track's part of the open section, side by
    // side, as {track, name, selected, rows: [{active, noteName, velocityHex,
    // lockText, hasLock} per step]}. A tracker that shows one channel at a
    // time is a step list, so the whole section is read across at once.
    Q_PROPERTY(QVariantList trackerChannels READ trackerChannels NOTIFY patternChanged)
    // The tracker's channels without their rows, as {track, name, selected,
    // hasPart}: what the header and the cursor need, without converting
    // every row of a long pattern each time one channel is looked at.
    Q_PROPERTY(QVariantList trackerHeads READ trackerHeads NOTIFY patternChanged)
    // Counts the pattern's changes. An editor that reads one step at a time
    // (stepRow, trackerCell, stemStep) reads it too, so the step is read
    // again whenever the pattern changes; a long pattern then costs what the
    // editor shows rather than every step it holds.
    Q_PROPERTY(int revision READ revision NOTIFY patternChanged)

public:
    // A step is a sixteenth note, whatever the meter or the pattern's length.
    static constexpr blokkily::Tick ticks_per_step = 120;
    [[nodiscard]] std::vector<std::vector<blokkily::Trigger>> rows() const;
    void shiftRows(const std::vector<std::vector<blokkily::Trigger>>& held, int from, int by);
    enum Role { IdRole = Qt::UserRole + 1, StepRole, KeyRole, NameRole, DurationRole,
                VelocityRole, LockRole, VoiceKeysRole, VoiceVelocitiesRole, VoiceLengthsRole };

    explicit PatternModel(SongModel* song, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // The narration feed (see AppController::eventBus): the guide's action
    // dispatcher stamps source=guide through setNextEventSource; the window's
    // own edits stay source=user.
    void setEventBus(blokkily::guide::EventBus* bus) { bus_ = bus; }
    void setNextEventSource(blokkily::guide::EventSource source) { next_source_ = source; }

    QVariantList steps() const;
    QVariantList stemRows() const;
    QVariantList trackerChannels() const;
    QVariantList trackerHeads() const;
    int revision() const noexcept { return revision_; }
    // One step of the open pattern, as steps() holds it.
    Q_INVOKABLE QVariantMap stepRow(int step) const;
    // One row of track `track`'s channel, as trackerChannels() holds it.
    Q_INVOKABLE QVariantMap trackerCell(int track, int step) const;
    // Whether track `track`'s part of the open section has a note on `step`,
    // as stemRows() holds it.
    Q_INVOKABLE bool stemStep(int track, int step) const;
    QVariantList controls() const;
    QVariantMap selected() const;
    int selectedStep() const noexcept { return selected_step_; }
    int lowKey() const;
    int highKey() const;
    // Moves the piano roll's pitch window up (positive) or down by
    // `semitones`, keeping how many keys it shows and stopping at 0 and 127.
    // Looking is not editing: no history, and the engine is not told. The
    // window follows the pattern again when another pattern is opened.
    Q_INVOKABLE void scrollRoll(int semitones);
    // The open pattern's length in steps; a length that is not a whole number
    // of steps counts its last, partial step.
    int stepCount() const;

    int patternTicks() const;

    // The controller lane's edits (wave 4.1), each one step of history unless
    // a gesture (SongModel::beginGesture) makes a whole stroke one. A lane is
    // one controller: `kind` "bend", "pressure", or "cc" with `controller`
    // its number (0..119, not the pedal). Levels are the lane's: -1..1 for
    // the wheel (0 centred), 0..1 otherwise. They write the canonical
    // pattern, so every projection and the engine follow.
    //
    // Draws a straight stroke from (fromTick, fromLevel) to (toTick,
    // toLevel): what the lane held between them is replaced by a point every
    // 15 ticks (a 32nd note) and one on each end.
    Q_INVOKABLE void drawControl(const QString& kind, int controller, int fromTick,
                                 double fromLevel, int toTick, double toLevel);
    // Moves the lane's point at `fromTick` to `toTick` at `level`, replacing
    // a point already there. False when there is no point at `fromTick`.
    Q_INVOKABLE bool moveControl(const QString& kind, int controller, int fromTick, int toTick,
                                 double level);
    // Removes the lane's points from `fromTick` to `toTick`, both included;
    // how many.
    Q_INVOKABLE int eraseControls(const QString& kind, int controller, int fromTick, int toTick);

    Q_INVOKABLE void toggleStep(int step, int key = 60);
    // Toggles a step on track `track`'s part of the open section, from the
    // stem rack: the track is selected, so the editors follow it there.
    Q_INVOKABLE void toggleStemStep(int track, int step, int key = 60);
    // Puts the tracker's cursor on row `step` of channel `track`: the track
    // is selected, so every editor and the keys that write follow it there.
    Q_INVOKABLE void selectChannelStep(int track, int step);
    Q_INVOKABLE bool hasStep(int step) const;
    Q_INVOKABLE int stepDuration(int step) const;
    Q_INVOKABLE int stepKey(int step) const;
    Q_INVOKABLE void selectStep(int step);
    // Tracker pitch entry replaces the step's pitch while preserving its
    // other settings. Every editor projects the same canonical pattern.
    Q_INVOKABLE void setStepKey(int step, int key);
    // Piano-roll gestures address a pitch within a step; tracker edits above
    // still replace the whole step. All edits use the same canonical pattern.
    Q_INVOKABLE void addRollNote(int step, int key);
    Q_INVOKABLE void eraseRollNote(int step, int key);
    Q_INVOKABLE int rollNoteDuration(int step, int key) const;
    Q_INVOKABLE void resizeRollNote(int step, int key, int ticks);
    Q_INVOKABLE bool moveRollNote(int from, int key, int to, int newKey);

    // Clears the whole step, as requested by the tracker's clear key.
    Q_INVOKABLE void clearStep(int step);
    // How long the whole step sounds, in ticks.
    Q_INVOKABLE void setStepDuration(int step, int ticks);
    Q_INVOKABLE void setSelectedDuration(int ticks);
    // Velocity from the tracker's VEL column, which is an input rather than a
    // readout of the inspector.
    Q_INVOKABLE void setStepVelocity(int step, double velocity);
    // The selected step, as a tracker copies a row: pitch, length, velocity,
    // locks and the rest. An empty row copies as empty, so pasting it clears.
    Q_INVOKABLE void copySelected();
    Q_INVOKABLE bool pasteSelected();
    // Puts a copy of the selected step onto the next row and moves the cursor
    // there, which is how a tracker fills a phrase without leaving the keys.
    Q_INVOKABLE bool duplicateSelected();
    // Inserts an empty row at the cursor and pushes later rows down; the last
    // row falls off the end of the pattern. Delete with shift pulls later rows
    // up into the hole.
    Q_INVOKABLE bool insertStep(int step);
    Q_INVOKABLE bool deleteAndShift(int step);
    // The pitches a step sounds, in the tuning it was written in. A chord is
    // one step with several voices, so this answers with all of them.
    [[nodiscard]] std::vector<blokkily::TunedPitch> pitchesAt(int step) const;
    // What a playable surface writes: the pitch or the chord that was pressed,
    // placed on a step of the same canonical pattern every editor projects.
    void placeNote(int step, const blokkily::Note& note);
    void placeChord(int step, const blokkily::Chord& chord);
    // Says that a recorded take wrote into the song's patterns directly. The
    // editors redraw and the engine is handed the new arrangement, as for any
    // other edit.
    void notifyRecorded();

    // Quantize and humanize (blokkily/model/groove.hpp) the open part, every
    // step of it, as one step of history. `gridTicks` is 120 for sixteenths,
    // 240 eighths, 480 beats; strength 0..1. Humanize moves each step up to
    // `timingTicks` (at most 59) and each velocity up to `velocity`, from
    // `seed`. Each returns how many steps it changed.
    Q_INVOKABLE int quantizePattern(int gridTicks, double strength = 1.0);
    Q_INVOKABLE int humanizePattern(int timingTicks, double velocity, int seed);

    // Step inspector edits. Each one rewrites the selected trigger in place, so
    // every projection updates from the same change.
    Q_INVOKABLE void transposeSelected(int semitones);
    // On a chord this moves the whole chord: a chord struck as one takes the
    // velocity, a chord struck voice by voice keeps its balance with its
    // loudest voice at the velocity.
    Q_INVOKABLE void setSelectedVelocity(double velocity);
    // One voice of the selected chord, by interval index, as the inspector's
    // per-voice bars edit it. The other voices keep their velocities.
    Q_INVOKABLE void setSelectedVoiceVelocity(int voice, double velocity);
    Q_INVOKABLE void setSelectedProbability(double probability);
    Q_INVOKABLE void setSelectedRatchets(int ratchets);
    Q_INVOKABLE void setSelectedMicroOffset(int ticks);
    Q_INVOKABLE void setSelectedPlayOnLoop(int loop);
    // Turns the selected chord through inversions, writing the voices it
    // turns into so every editor shows them: up lifts the lowest voice an
    // octave, down drops the highest. One step of history; a single note or
    // a voice that would leave the keyboard changes nothing.
    Q_INVOKABLE void invertSelected(int turns);
    // Ticks between one voice of the selected chord and the next, lowest
    // first, 0 to 60 (half a step). Nothing for a single note.
    Q_INVOKABLE void setSelectedStrum(int ticks);
    // Sets the selected step's lock to `value` (0..1) of parameter `index`,
    // as automation or as modulation. A lock already there keeps what it
    // targets (setSelectedLockTarget); a new one locks the instrument.
    Q_INVOKABLE void setSelectedLock(int index, double value, bool modulation);
    // Points the selected step's lock at parameter `parameter` (named `name`)
    // of processor `slot` of its track: -1 the instrument, 0.. an insert
    // effect. A step with no lock gains one at 0.5, as automation.
    Q_INVOKABLE void setSelectedLockTarget(int slot, int parameter, const QString& name);
    // The same, for insert `slot` (0..) of the bus `kind` ("track" the step's
    // own track, "return" return `bus`, "master" the master bus).
    Q_INVOKABLE void setSelectedLockTargetOn(const QString& kind, int bus, int slot, int parameter,
                                             const QString& name);
    Q_INVOKABLE void clearSelectedLock();
    // Locks one parameter on the note that starts on `step`, beside any other
    // locks the step holds: a lock already on the same processor and
    // parameter takes `lock`'s value and kind. One step of history. False when
    // no note starts there.
    bool lockStep(int step, const blokkily::ParameterLock& lock);
    // Takes every lock off the note on `step`. False when no note starts there.
    bool clearStepLocks(int step);

    const blokkily::Pattern& pattern() const;
    void replace(blokkily::Pattern pattern);
    // Re-reads the song after the current pattern or the whole session changed.
    void refresh();

signals:
    // What the editors must redraw: an edit, a reload, or the song opening a
    // different pattern.
    void patternChanged();
    // What the song actually plays. Only a real edit raises this, so looking
    // at another pattern or moving a fader never reaches the audio engine.
    void contentChanged();
    // The cursor moved; the tracker scrolls to it.
    void selectionChanged();
    // What `selected` reads may differ.
    void selectedChanged();

private:
    const blokkily::Trigger* triggerAt(int step) const;
    QVariantList noteExpression() const;
    bool editNoteExpression(const QString& kind, int voice, int fromTick, int toTick,
                            const std::function<void(std::vector<blokkily::NoteExpression>&,
                                                     blokkily::NoteExpression::Kind, blokkily::Tick,
                                                     blokkily::Tick)>& edit);
    // `merge` names a control whose moves arrive as a stream, so dragging it
    // is one step of history.
    void mutate(int step, const std::function<void(blokkily::Trigger&)>& edit,
                const QString& merge = {});

    blokkily::Pattern& pattern();
    SongModel* song_ = nullptr;
    int revision_ = 0;
    int selected_step_ = -1;
    bool has_clipboard_ = false;
    std::optional<blokkily::Trigger> clipboard_;
    // The roll's window once scrolled: its lowest key and how many keys it
    // spans, and the pattern it was scrolled on.
    std::optional<int> roll_low_;
    int roll_span_ = 0;
    int roll_pattern_ = -1;
    // The narration feed (see AppController::eventBus): note edits append
    // here. The guide's action dispatcher stamps source=guide through the
    // setter; the window's own edits stay source=user.
    blokkily::guide::EventBus* bus_ = nullptr;
    blokkily::guide::EventSource next_source_ = blokkily::guide::EventSource::user;
    void emitBusEvent(blokkily::guide::EventKind kind, const std::string& label,
                      const std::string& detail = "");
};
