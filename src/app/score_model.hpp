#pragma once

#include "blokkily/notation/clef.hpp"
#include "blokkily/notation/signature.hpp"
#include "blokkily/notation/staff.hpp"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <utility>
#include <vector>

class PatternModel;
class SongModel;

// The score view's projection of the canonical pattern: the open part laid
// out as staff notation, rebuilt whenever the pattern, the selection or the
// song's notation settings change. Notes entered or edited on the staff are
// written into the pattern through the same edits the piano roll makes, and
// come back from it - the pattern stays the single source of truth (repo
// invariant). The clef, key and meter
// it shows are the song's (SongModel::notationSettings for the selected
// track's part of the open section); setting them here sets them there, as
// one step of undo.
//
// Layout is done here, not in QML, so the gate can measure it: every object
// carries its x in staff spaces from the first note, and the barlines,
// content width and the highest and lowest staff positions are given so the
// view can size and scroll the staff instead of clipping it.
class ScoreModel final : public QObject {
    Q_OBJECT
    // Which clef the staff is drawn under, as an index into clefNames().
    Q_PROPERTY(int clefKind READ clefKind WRITE setClefKind NOTIFY scoreChanged)
    // Key signature in fifths, -7..7.
    Q_PROPERTY(int keyFifths READ keyFifths WRITE setKeyFifths NOTIFY scoreChanged)
    // The meter the part is laid out in: the section's, or the song's.
    Q_PROPERTY(int timeBeats READ timeBeats NOTIFY scoreChanged)
    Q_PROPERTY(int timeBeatUnit READ timeBeatUnit NOTIFY scoreChanged)
    Q_PROPERTY(bool unmetered READ unmetered WRITE setUnmetered NOTIFY scoreChanged)
    // Notes and rests, each a map: kind ("note"/"rest"), x (staff spaces),
    // pos (diatonic steps above the clef's bottom line), acc (-2..2),
    // showAcc, value (NoteValue as int), dots, voice, stemUp, stem (this
    // note draws its chord's stem, from stemFrom to stemTo positions),
    // stemless, ledger (ledger lines needed), tieStart, tieToX, lyric (the
    // syllable without its hyphen), hyphen and hyphenToX (the word goes on
    // to the next sung note, at that x), extend (holds the previous
    // syllable), tuplet and tupletEndX (on the first note of a tuplet group),
    // step and key (the pattern note it draws: its step, -1 off the grid,
    // and its sounding MIDI key) and selected.
    Q_PROPERTY(QVariantList objects READ objects NOTIFY scoreChanged)
    // Each barline's x in staff spaces, and whether it is the final one.
    Q_PROPERTY(QVariantList barlines READ barlines NOTIFY scoreChanged)
    // The key signature's accidentals in display order, each {pos, acc}.
    Q_PROPERTY(QVariantList keySignature READ keySignature NOTIFY scoreChanged)
    Q_PROPERTY(QString clefName READ clefName NOTIFY scoreChanged)
    Q_PROPERTY(QString keyLabel READ keyLabel NOTIFY scoreChanged)
    Q_PROPERTY(int objectCount READ objectCount NOTIFY scoreChanged)
    Q_PROPERTY(int measureCount READ measureCount NOTIFY scoreChanged)
    Q_PROPERTY(double contentWidth READ contentWidth NOTIFY scoreChanged)
    // The highest and lowest staff positions anything is drawn at (staff
    // lines included), so the view can fit the system.
    Q_PROPERTY(int highestPos READ highestPos NOTIFY scoreChanged)
    Q_PROPERTY(int lowestPos READ lowestPos NOTIFY scoreChanged)
    // Staff position of the clef's reference line (the G/F/C line the clef
    // glyph's origin sits on; 4 for percussion).
    Q_PROPERTY(int clefRefPos READ clefRefPos NOTIFY scoreChanged)
    // The value a click on the staff writes: 1 whole, 2 half, 4 quarter, 8
    // eighth, 16 sixteenth; dotted adds half again.
    Q_PROPERTY(int noteValue READ noteValue WRITE setNoteValue NOTIFY entryChanged)
    Q_PROPERTY(bool dotted READ dotted WRITE setDotted NOTIFY entryChanged)
    // The note selected on the staff (its step and key), -1 for none.
    Q_PROPERTY(int selectedStep READ selectedStep NOTIFY scoreChanged)
    Q_PROPERTY(int selectedKey READ selectedKey NOTIFY scoreChanged)
    // Whether the score is on screen. Changes it follows are laid out once
    // per turn of the event loop while it is, and once it is shown again
    // while it is not.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    ScoreModel(PatternModel* pattern, SongModel* song, QObject* parent = nullptr);

    int clefKind() const noexcept { return clef_kind_; }
    void setClefKind(int kind);
    int keyFifths() const noexcept { return key_fifths_; }
    void setKeyFifths(int fifths);
    int timeBeats() const noexcept { return time_beats_; }
    int timeBeatUnit() const noexcept { return time_beat_unit_; }
    bool unmetered() const noexcept { return unmetered_; }
    void setUnmetered(bool unmetered);

    QVariantList objects() const { return objects_; }
    QVariantList barlines() const { return barlines_; }
    QVariantList keySignature() const { return key_sig_; }
    QString clefName() const;
    QString keyLabel() const;
    int objectCount() const noexcept { return static_cast<int>(objects_.size()); }
    int measureCount() const noexcept { return measure_count_; }
    double contentWidth() const noexcept { return content_width_; }
    int highestPos() const noexcept { return highest_pos_; }
    int lowestPos() const noexcept { return lowest_pos_; }
    int clefRefPos() const;

    // The clef roster for the view's picker, in ClefKind order.
    Q_INVOKABLE QStringList clefNames() const;
    // Rebuild now (also runs on every change it follows).
    Q_INVOKABLE void refresh();

    int noteValue() const noexcept { return note_value_; }
    void setNoteValue(int value);
    bool dotted() const noexcept { return dotted_; }
    void setDotted(bool dotted);
    bool active() const noexcept { return active_; }
    void setActive(bool active);
    int selectedStep() const noexcept { return selected_step_; }
    int selectedKey() const noexcept { return selected_key_; }

    // The staff's two axes. tickAt is the step-aligned tick under x (staff
    // spaces, as objects' x), to the nearest step, -1 off the part; xForTick is its inverse.
    // keyAt is the sounding key written at staff position pos, with the key
    // signature's accidental.
    Q_INVOKABLE int tickAt(double x) const;
    Q_INVOKABLE double xForTick(int tick) const;
    Q_INVOKABLE int keyAt(int pos) const;
    // A click on the staff at (x, pos): on a notehead it selects that note;
    // anywhere else it writes a note of noteValue there - beside any notes
    // already sounding at that time, as a chord - and selects it. True when
    // it did either. One step of undo.
    Q_INVOKABLE bool click(double x, double pos);
    Q_INVOKABLE void deselect();
    // Edits of the selected note, each one step of undo: remove it, move it
    // `steps` lines and spaces (7 an octave), or raise or lower it by
    // semitones. False when nothing is selected or the move would land on a
    // note already there.
    Q_INVOKABLE bool deleteSelected();
    Q_INVOKABLE bool moveSelected(int steps);
    Q_INVOKABLE bool alterSelected(int semitones);

signals:
    void scoreChanged();
    void entryChanged();
    void activeChanged();

private:
    void rebuild();
    // A change it follows: rebuilt on the next turn of the event loop, or
    // when the score is next shown.
    void queueRebuild();
    void layout(const blokkily::notation::Staff& staff);
    void write(const QVariantMap& settings);
    bool select(int step, int key);
    QVariantMap selectedObject() const;

    PatternModel* pattern_ = nullptr;
    SongModel* song_ = nullptr;
    int clef_kind_ = static_cast<int>(blokkily::notation::ClefKind::Treble);
    int key_fifths_ = 0;
    int time_beats_ = 4;
    int time_beat_unit_ = 4;
    bool unmetered_ = false;
    QVariantList objects_;
    QVariantList barlines_;
    QVariantList key_sig_;
    int measure_count_ = 0;
    double content_width_ = 0.0;
    int highest_pos_ = 8;
    int lowest_pos_ = 0;
    int transposition_ = 0;
    int note_value_ = 4;
    bool dotted_ = false;
    int selected_step_ = -1;
    int selected_key_ = -1;
    bool active_ = true;
    bool stale_ = false;  // a change came while hidden or not yet laid out
    bool queued_ = false; // a rebuild is waiting for the event loop
    // (x, tick) at every column and barline, in order: the staff's time axis.
    std::vector<std::pair<double, blokkily::Tick>> time_map_;
};
