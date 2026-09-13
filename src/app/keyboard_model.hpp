#pragma once

#include "pattern_model.hpp"
#include "song_model.hpp"

#include "blokkily/model/keyboard.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

// The playable surface, laid out in the song's own tuning and scale. Piano
// keys, an isomorphic grid, a fretboard, and chord pads are the same object
// seen four ways: each produces cells, and pressing a cell sounds it through
// the running engine and writes it onto the step the editors have selected.
// Nothing here keeps a second copy of the tuning, the scale, or the pattern.
class KeyboardModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString surface READ surface NOTIFY specChanged)
    Q_PROPERTY(QStringList surfaces READ surfaces CONSTANT)
    Q_PROPERTY(QVariantList cells READ cells NOTIFY specChanged)
    Q_PROPERTY(int rows READ rows NOTIFY specChanged)
    Q_PROPERTY(int columns READ columns NOTIFY specChanged)
    // How much room the laid-out surface needs, in cell widths and heights.
    // Not the same as the row and column counts: staggered rows are half a cell
    // wider than their contents and hexagonal rows overlap, so what is drawn
    // is measured rather than counted.
    Q_PROPERTY(double spanX READ spanX NOTIFY specChanged)
    Q_PROPERTY(double spanY READ spanY NOTIFY specChanged)
    // "RECT" or "HEX": the tiling a cell is drawn in.
    Q_PROPERTY(QString cellShape READ cellShape NOTIFY specChanged)
    Q_PROPERTY(QString orientation READ orientation NOTIFY specChanged)
    Q_PROPERTY(QStringList orientations READ orientations CONSTANT)
    Q_PROPERTY(QString layoutName READ layoutName NOTIFY specChanged)
    Q_PROPERTY(QStringList layoutNames READ layoutNames CONSTANT)
    Q_PROPERTY(QString registerName READ registerName NOTIFY specChanged)
    Q_PROPERTY(QStringList registerNames READ registerNames CONSTANT)
    Q_PROPERTY(QString stringTuningName READ stringTuningName NOTIFY specChanged)
    Q_PROPERTY(QStringList stringTuningNames READ stringTuningNames CONSTANT)
    // Whether a press is written onto the selected step as well as sounded.
    Q_PROPERTY(bool recording READ recording NOTIFY specChanged)
    // What the last press produced, so the interface can say what was heard
    // even when no instrument was loaded to hear it through.
    Q_PROPERTY(QString lastPlayed READ lastPlayed NOTIFY played)

public:
    KeyboardModel(SongModel* song, PatternModel* pattern, AppController* controller,
                  QObject* parent = nullptr);

    QString surface() const;
    QStringList surfaces() const;
    QVariantList cells() const;
    int rows() const;
    int columns() const;
    double spanX() const;
    double spanY() const;
    QString cellShape() const;
    QString orientation() const;
    QStringList orientations() const;
    QString layoutName() const { return QString::fromStdString(spec_.isomorphic.name); }
    QStringList layoutNames() const;
    QString registerName() const { return QString::fromStdString(spec_.range.name); }
    QStringList registerNames() const;
    QString stringTuningName() const { return QString::fromStdString(spec_.strings.name); }
    QStringList stringTuningNames() const;
    bool recording() const noexcept { return recording_; }
    QString lastPlayed() const { return last_played_; }

    Q_INVOKABLE void setSurface(const QString& name);
    Q_INVOKABLE void setLayout(const QString& name);
    Q_INVOKABLE void setRegister(const QString& name);
    Q_INVOKABLE void setStringTuning(const QString& name);
    Q_INVOKABLE void setOrientation(const QString& name);
    Q_INVOKABLE void toggleOrientation();
    Q_INVOKABLE void toggleRecording();
    // Presses the cell at `index` of `cells`. Sounds it, and — while recording
    // — writes it onto the step the editors have selected.
    Q_INVOKABLE bool press(int index);
    // Presses whichever cell sounds a degree, which is how a computer key or a
    // test drives the surface without knowing how it is laid out.
    Q_INVOKABLE bool pressDegree(int degree);

    // The cell list as the model sees it, for the verification gate.
    [[nodiscard]] const std::vector<blokkily::KeyboardCell>& layout() const { return cells_; }

signals:
    // The surface itself changed: a different layout, tuning, scale, or root.
    void specChanged();
    // A cell was pressed. Carries the degree so a listener need not re-read the
    // layout to know what was played.
    void played(int degree);

private:
    // Re-reads the tuning and scale from the song and rebuilds the layout.
    void rebuild();
    [[nodiscard]] const blokkily::KeyboardCell* cellForDegree(int degree) const;

    SongModel* song_ = nullptr;
    PatternModel* pattern_ = nullptr;
    AppController* controller_ = nullptr;
    blokkily::KeyboardSpec spec_;
    std::vector<blokkily::KeyboardCell> cells_;
    bool recording_ = true;
    QString last_played_ = QStringLiteral("—");
};
