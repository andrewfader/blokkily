#pragma once

#include "blokkily/notation/clef.hpp"
#include "blokkily/notation/signature.hpp"

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <vector>

class PatternModel;

// The score view's projection of the canonical pattern: the pattern's notes
// re-spelled as staff notation, rebuilt on every pattern change. Read-only —
// it never writes back into the pattern, so the pattern stays the single
// source of truth (repo invariant). QML renders `objects` (notes and rests in
// time order, each already carrying its staff position, accidental, value,
// dots, lyric and ties) plus the staff furniture (clef, key signature,
// time signature) the view draws around them.
class ScoreModel final : public QObject {
    Q_OBJECT
    // Which clef the staff is drawn under, as an index into clefNames().
    Q_PROPERTY(int clefKind READ clefKind WRITE setClefKind NOTIFY scoreChanged)
    // Key signature in fifths, -7..7.
    Q_PROPERTY(int keyFifths READ keyFifths WRITE setKeyFifths NOTIFY scoreChanged)
    // Meter of the staff the pattern was laid out on (4/4, or unmetered).
    Q_PROPERTY(int timeBeats READ timeBeats NOTIFY scoreChanged)
    Q_PROPERTY(int timeBeatUnit READ timeBeatUnit NOTIFY scoreChanged)
    Q_PROPERTY(bool unmetered READ unmetered WRITE setUnmetered NOTIFY scoreChanged)
    // Written-pitch octave shift (treble-8vb is -1); sounding pitch moves
    // with it, the staff positions are the written ones.
    Q_PROPERTY(int transposeOctaves READ transposeOctaves WRITE setTransposeOctaves NOTIFY scoreChanged)
    // Notes and rests in time order, each a map: kind ("note"/"rest"), pos
    // (diatonic steps above the clef's bottom line, transposition applied),
    // acc (-2..2), showAcc, value (NoteValue as int), dots, lyric, tieStart,
    // tieEnd, stemUp, ledger (ledger lines needed), voice.
    Q_PROPERTY(QVariantList objects READ objects NOTIFY scoreChanged)
    // The key signature's accidentals in display order, each {pos, acc}.
    Q_PROPERTY(QVariantList keySignature READ keySignature NOTIFY scoreChanged)
    Q_PROPERTY(QString clefName READ clefName NOTIFY scoreChanged)
    Q_PROPERTY(QString keyLabel READ keyLabel NOTIFY scoreChanged)
    Q_PROPERTY(int objectCount READ objectCount NOTIFY scoreChanged)
    // Staff position of the clef's reference line (the G/F/C line the clef
    // glyph's origin sits on; 4 for percussion).
    Q_PROPERTY(int clefRefPos READ clefRefPos NOTIFY scoreChanged)

public:
    explicit ScoreModel(const PatternModel* pattern, QObject* parent = nullptr);

    int clefKind() const noexcept { return clef_kind_; }
    void setClefKind(int kind);
    int keyFifths() const noexcept { return key_fifths_; }
    void setKeyFifths(int fifths);
    int timeBeats() const noexcept { return time_beats_; }
    int timeBeatUnit() const noexcept { return time_beat_unit_; }
    bool unmetered() const noexcept { return unmetered_; }
    void setUnmetered(bool unmetered);
    int transposeOctaves() const noexcept { return transpose_octaves_; }
    void setTransposeOctaves(int octaves);

    QVariantList objects() const { return objects_; }
    QVariantList keySignature() const { return key_sig_; }
    QString clefName() const;
    QString keyLabel() const;
    int objectCount() const noexcept { return static_cast<int>(objects_.size()); }
    int clefRefPos() const;

    // The clef roster for the view's picker, in ClefKind order.
    Q_INVOKABLE QStringList clefNames() const;
    // Rebuild from the pattern now (also runs on patternChanged).
    Q_INVOKABLE void refresh();

signals:
    void scoreChanged();

private:
    void rebuild();

    const PatternModel* pattern_ = nullptr;
    int clef_kind_ = static_cast<int>(blokkily::notation::ClefKind::Treble);
    int key_fifths_ = 0;
    int time_beats_ = 4;
    int time_beat_unit_ = 4;
    bool unmetered_ = false;
    int transpose_octaves_ = 0;
    QVariantList objects_;
    QVariantList key_sig_;
};
