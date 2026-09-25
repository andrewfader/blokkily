#pragma once

#include "blokkily/model/song.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cstddef>
#include <optional>
#include <vector>

// The session, as the interface sees it: named patterns, mixer tracks, and the
// clips that arrange one over the other. Every editor and the audio engine read
// this one object, so the arrangement, the mixer, and what is heard cannot
// drift apart.
class SongModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY songChanged)
    Q_PROPERTY(QVariantList patterns READ patterns NOTIFY songChanged)
    Q_PROPERTY(QVariantList clips READ clips NOTIFY songChanged)
    // The timeline as the arrangement draws it: one lane per track, one entry
    // per bar. A property rather than a lookup call, so the view re-reads it
    // whenever the song changes.
    Q_PROPERTY(QVariantList lanes READ lanes NOTIFY songChanged)
    Q_PROPERTY(int trackCount READ trackCount NOTIFY songChanged)
    Q_PROPERTY(int currentPattern READ currentPattern NOTIFY songChanged)
    Q_PROPERTY(int selectedTrack READ selectedTrack NOTIFY songChanged)
    Q_PROPERTY(int bars READ bars NOTIFY songChanged)
    Q_PROPERTY(double masterGainDb READ masterGainDb NOTIFY mixChanged)
    Q_PROPERTY(double masterPeak READ masterPeak NOTIFY metersChanged)
    // Each track's meter, as the fraction of its bar to light. Kept apart from
    // `tracks` so thirty meter updates a second do not re-read the whole song.
    Q_PROPERTY(QVariantList meters READ meters NOTIFY metersChanged)
    // Edit history. Every change to what the song is — a step, a clip, a
    // track, a fader, the tuning — can be taken back and done again.
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    // Whether the song differs from what was last saved or opened. Undoing
    // back to the saved state makes it clean again.
    Q_PROPERTY(bool dirty READ dirty NOTIFY historyChanged)
    // The tuning and scale the whole session is written in. Every editor, the
    // keyboards, and the engine read these, so nothing on screen can be in a
    // different key from the notes it is editing.
    Q_PROPERTY(QString tuningName READ tuningName NOTIFY tuningChanged)
    Q_PROPERTY(QStringList tuningNames READ tuningNames CONSTANT)
    Q_PROPERTY(int divisions READ divisions NOTIFY tuningChanged)
    Q_PROPERTY(QString scaleName READ scaleName NOTIFY tuningChanged)
    Q_PROPERTY(QStringList scaleNames READ scaleNames CONSTANT)
    Q_PROPERTY(int rootDegree READ rootDegree NOTIFY tuningChanged)
    Q_PROPERTY(QString rootName READ rootName NOTIFY tuningChanged)
    Q_PROPERTY(bool autoScale READ autoScale NOTIFY tuningChanged)

public:
    // One bar of 4/4 at the model's resolution. The arrangement is laid out in
    // bars because that is how a producer reads a timeline.
    static constexpr blokkily::Tick ticks_per_bar = 1920;
    static constexpr int minimum_visible_bars = 8;

    explicit SongModel(QObject* parent = nullptr);

    QVariantList tracks() const;
    QVariantList patterns() const;
    QVariantList clips() const;
    QVariantList lanes() const;
    int trackCount() const noexcept { return static_cast<int>(song_.tracks.size()); }
    int currentPattern() const noexcept { return current_pattern_; }
    int selectedTrack() const noexcept { return selected_track_; }
    int bars() const;
    double masterGainDb() const noexcept { return song_.master_gain_db; }
    double masterPeak() const noexcept { return master_peak_; }
    QVariantList meters() const;
    bool canUndo() const noexcept { return !undo_.empty(); }
    bool canRedo() const noexcept { return !redo_.empty(); }
    bool dirty() const noexcept { return state_id_ != saved_id_; }
    QString tuningName() const;
    QStringList tuningNames() const;
    int divisions() const noexcept { return song_.tuning.divisions(); }
    QString scaleName() const;
    QStringList scaleNames() const;
    int rootDegree() const noexcept { return song_.root_degree; }
    QString rootName() const;
    bool autoScale() const noexcept { return song_.auto_scale; }

    Q_INVOKABLE void selectTrack(int track);
    Q_INVOKABLE void selectPattern(int pattern);
    Q_INVOKABLE void addTrack();
    // A new track that arrives already carrying an instrument, as one edit and
    // one rebuild of the audio graph rather than two.
    void addTrack(const blokkily::InstrumentSlot& instrument);
    Q_INVOKABLE void addPattern();
    // Pattern and track housekeeping. A copy of the open pattern is how a
    // variation is started; a name is how a lane is found again.
    Q_INVOKABLE void duplicatePattern();
    Q_INVOKABLE void clearPattern();
    Q_INVOKABLE bool deletePattern(int pattern);
    Q_INVOKABLE bool deleteTrack(int track);
    // While structureChanged is being emitted for a deleted track: where each
    // track before the deletion went (entry i is old track i's new index, or
    // empty for the one removed). Null at any other time, so it can never be
    // applied to a later change.
    const std::vector<std::optional<std::size_t>>* pendingTrackRemap() const {
        return track_remap_ ? &*track_remap_ : nullptr;
    }
    Q_INVOKABLE void renamePattern(int pattern, const QString& name);
    Q_INVOKABLE void renameTrack(int track, const QString& name);
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    // Mixer moves. These change how the song sounds without changing what it
    // plays, so they are reported separately and never rebuild the engine.
    Q_INVOKABLE void setTrackGain(int track, double decibels);
    Q_INVOKABLE void setTrackPan(int track, double pan);
    Q_INVOKABLE void toggleMute(int track);
    Q_INVOKABLE void toggleSolo(int track);
    Q_INVOKABLE void setMasterGain(double decibels);
    // Arrangement editing. An empty bar takes the open pattern; a filled bar
    // is opened rather than erased — the right button takes a clip away.
    Q_INVOKABLE void placeClip(int track, int bar);
    Q_INVOKABLE bool removeClip(int track, int bar);
    Q_INVOKABLE void toggleClip(int track, int bar);
    // Opens whatever clip covers that cell: selects its track and its pattern.
    // Returns the pattern index, or -1 when the cell is empty.
    Q_INVOKABLE int openClip(int track, int bar);
    // How many times the clip covering that bar runs back-to-back. Lengthening
    // a clip is not the same as placing another of the same pattern — each
    // repetition is the next loop for probability and loop conditions.
    Q_INVOKABLE bool setClipRepeats(int track, int bar, int repeats);
    // Moves the clip covering `bar` so it starts at `newBar` on the same track.
    Q_INVOKABLE bool moveClip(int track, int bar, int newBar);
    Q_INVOKABLE bool hasClip(int track, int bar) const;
    // Tuning and scale moves. They change how the session is written and read,
    // never the notes already in it: a pattern keeps the pitches it was played
    // at, so changing tuning re-reads the music rather than rewriting it.
    Q_INVOKABLE void setTuning(const QString& name);
    Q_INVOKABLE void setScale(const QString& name);
    Q_INVOKABLE void setRootDegree(int degree);
    Q_INVOKABLE void toggleAutoScale();
    // What an instrument must be told to sound a degree, and what that degree
    // is called. The keyboards and the editors both ask through here so a note
    // has one name everywhere.
    Q_INVOKABLE QString degreeName(int degree) const;
    QString pitchName(int key, double cents) const;
    [[nodiscard]] blokkily::TunedPitch pitchForDegree(int degree) const;
    // The degree a played one becomes once the scale has its say.
    [[nodiscard]] int snapDegree(int degree) const;

    blokkily::Song& song() noexcept { return song_; }
    const blokkily::Song& song() const noexcept { return song_; }
    blokkily::Pattern& editPattern();
    const blokkily::Pattern& editPattern() const;
    void replace(blokkily::Song song);
    void setInstrument(int track, const blokkily::InstrumentSlot& slot);
    // Says that the tracks, their instruments, or the clips over them were
    // changed through `song()` directly. Several such changes made together
    // reach the editors and the engine as one, rather than as one rebuild per
    // track.
    void refreshStructure();
    void setMeters(const std::vector<float>& track_peaks, float master_peak);
    // Announces a change the audio engine has to be rebuilt for.
    void notifyStructureChanged();
    // Records the song as it is, before an edit changes it, so the edit can be
    // undone. Edits that arrive as a stream — a fader or a slider being dragged
    // — pass the same `merge` key for every move, and the whole gesture is
    // then one step of history rather than one per pixel.
    void checkpoint(const QString& merge = {});
    // A pointer gesture — a stroke drawn across the piano roll, a note dragged
    // up the tracker — is one step of history however many edits it makes.
    Q_INVOKABLE void beginGesture();
    Q_INVOKABLE void endGesture();
    // The song on disk now matches the song in memory.
    void markSaved();
    // A new or freshly opened session has no history to go back through.
    void clearHistory();

signals:
    void songChanged();
    // Raised only when the arrangement itself changed, so the audio engine is
    // rebuilt for a new clip or instrument but not for a fader move.
    void structureChanged();
    void mixChanged();
    void metersChanged();
    // The session changed tuning, scale, or root. Every projection that names
    // or draws a pitch re-reads itself from this.
    void tuningChanged();
    void historyChanged();

private:
    [[nodiscard]] bool validTrack(int track) const;
    // The clip covering a bar on a track, if any. One definition of coverage,
    // shared by the lanes projection and by editing.
    [[nodiscard]] const blokkily::Clip* clipAt(int track, int bar) const;
    [[nodiscard]] blokkily::Clip* clipAt(int track, int bar);

    // One entry of history: the whole song, and which state of it this was,
    // so returning to the saved state is recognised as clean.
    struct Snapshot {
        blokkily::Song song;
        int current_pattern = 0;
        int selected_track = 0;
        std::uint64_t id = 0;
    };
    Snapshot snapshot() const;
    void restore(Snapshot snapshot);

    blokkily::Song song_;
    int current_pattern_ = 0;
    int selected_track_ = 0;
    std::vector<float> peaks_;
    std::optional<std::vector<std::optional<std::size_t>>> track_remap_;
    float master_peak_ = 0.0F;
    std::vector<Snapshot> undo_;
    std::vector<Snapshot> redo_;
    std::uint64_t state_id_ = 0;
    std::uint64_t saved_id_ = 0;
    std::uint64_t next_id_ = 1;
    QString last_merge_;
    bool gesture_open_ = false;
    bool gesture_recorded_ = false;
    QElapsedTimer merge_clock_;
};
