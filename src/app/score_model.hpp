#pragma once

#include "blokkily/notation/clef.hpp"
#include "blokkily/notation/signature.hpp"
#include "blokkily/notation/staff.hpp"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class PatternModel;
class SongModel;

// The score view's projection of the canonical pattern: the open part laid
// out as staff notation, rebuilt whenever the pattern, the selection or the
// song's notation settings change. It never writes notes back - the pattern
// stays the single source of truth (repo invariant). The clef, key and meter
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
    // syllable), tuplet and tupletEndX (on the first note of a tuplet group).
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

public:
    ScoreModel(const PatternModel* pattern, SongModel* song, QObject* parent = nullptr);

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

signals:
    void scoreChanged();

private:
    void rebuild();
    void layout(const blokkily::notation::Staff& staff);
    void write(const QVariantMap& settings);

    const PatternModel* pattern_ = nullptr;
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
};
