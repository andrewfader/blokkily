#pragma once

#include "song_model.hpp"

#include "blokkily/model/pattern.hpp"

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
    // Where the cursor is, kept apart from what the pattern holds: moving the
    // cursor is not an edit, and nothing downstream may treat it as one.
    Q_PROPERTY(int selectedStep READ selectedStep NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY selectionChanged)
    // Pitch window the piano roll draws, widened to whatever the pattern uses.
    Q_PROPERTY(int lowKey READ lowKey NOTIFY patternChanged)
    Q_PROPERTY(int highKey READ highKey NOTIFY patternChanged)
    // How many sixteenth steps the open pattern has: sixteen for a 4/4 bar,
    // fourteen for a 7/8 one, or whatever its LEN was set to. The grid, the
    // tracker and the roll each draw this many columns.
    Q_PROPERTY(int stepCount READ stepCount NOTIFY patternChanged)

public:
    // A step is a sixteenth note, whatever the meter or the pattern's length.
    static constexpr blokkily::Tick ticks_per_step = 120;
    enum Role { IdRole = Qt::UserRole + 1, StepRole, KeyRole, NameRole, DurationRole,
                VelocityRole, LockRole, VoiceKeysRole, VoiceVelocitiesRole, VoiceLengthsRole };

    explicit PatternModel(SongModel* song, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QVariantList steps() const;
    QVariantMap selected() const;
    int selectedStep() const noexcept { return selected_step_; }
    int lowKey() const;
    int highKey() const;
    // The open pattern's length in steps; a length that is not a whole number
    // of steps counts its last, partial step.
    int stepCount() const;

    Q_INVOKABLE void toggleStep(int step, int key = 60);
    Q_INVOKABLE bool hasStep(int step) const;
    Q_INVOKABLE int stepDuration(int step) const;
    Q_INVOKABLE int stepKey(int step) const;
    Q_INVOKABLE void selectStep(int step);
    // What the tracker and the piano roll write. Both are editors rather than
    // read-outs, so both put notes on the same canonical pattern the step grid
    // and the keyboards do, and every projection redraws from that one change.
    // Writing over a step keeps everything else the producer set on it.
    Q_INVOKABLE void setStepKey(int step, int key);
    // Takes the note off a step without putting one back, which is what a
    // tracker's clear key and the roll's erase gesture mean. Toggling would put
    // a note back on a step the producer has just emptied.
    Q_INVOKABLE void clearStep(int step);
    // How long the step sounds, in ticks. The piano roll draws that width and
    // dragging a note's right edge writes it here, so a long note is a long
    // note in every editor and in what the engine plays.
    Q_INVOKABLE void setStepDuration(int step, int ticks);
    Q_INVOKABLE void setSelectedDuration(int ticks);
    // Velocity from the tracker's VEL column, which is an input rather than a
    // readout of the inspector.
    Q_INVOKABLE void setStepVelocity(int step, double velocity);
    // Moves a step in time and pitch together, which is what dragging a note
    // on the piano roll means. `semitones` transposes every voice, so a chord
    // stays a chord.
    Q_INVOKABLE void relocateStep(int from, int to, int semitones);
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
    Q_INVOKABLE void setSelectedLock(int index, double value, bool modulation);
    Q_INVOKABLE void clearSelectedLock();

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
    void selectionChanged();

private:
    const blokkily::Trigger* triggerAt(int step) const;
    // `merge` names a control whose moves arrive as a stream, so dragging it
    // is one step of history.
    void mutate(int step, const std::function<void(blokkily::Trigger&)>& edit,
                const QString& merge = {});

    blokkily::Pattern& pattern();
    SongModel* song_ = nullptr;
    int selected_step_ = -1;
    bool has_clipboard_ = false;
    std::optional<blokkily::Trigger> clipboard_;
};
