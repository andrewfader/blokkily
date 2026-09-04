#pragma once

#include "blokkily/model/song.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

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

    Q_INVOKABLE void selectTrack(int track);
    Q_INVOKABLE void selectPattern(int pattern);
    Q_INVOKABLE void addTrack();
    Q_INVOKABLE void addPattern();
    // Mixer moves. These change how the song sounds without changing what it
    // plays, so they are reported separately and never rebuild the engine.
    Q_INVOKABLE void setTrackGain(int track, double decibels);
    Q_INVOKABLE void setTrackPan(int track, double pan);
    Q_INVOKABLE void toggleMute(int track);
    Q_INVOKABLE void toggleSolo(int track);
    Q_INVOKABLE void setMasterGain(double decibels);
    // Arrangement editing: click a cell to place the current pattern there,
    // click it again to take it away.
    Q_INVOKABLE void toggleClip(int track, int bar);
    Q_INVOKABLE bool hasClip(int track, int bar) const;

    blokkily::Song& song() noexcept { return song_; }
    const blokkily::Song& song() const noexcept { return song_; }
    blokkily::Pattern& editPattern();
    const blokkily::Pattern& editPattern() const;
    void replace(blokkily::Song song);
    void setInstrument(int track, const blokkily::InstrumentSlot& slot);
    void setMeters(const std::vector<float>& track_peaks, float master_peak);
    // Announces a change the audio engine has to be rebuilt for.
    void notifyStructureChanged();

signals:
    void songChanged();
    // Raised only when the arrangement itself changed, so the audio engine is
    // rebuilt for a new clip or instrument but not for a fader move.
    void structureChanged();
    void mixChanged();
    void metersChanged();

private:
    [[nodiscard]] bool validTrack(int track) const;
    // The clip covering a bar on a track, if any. One definition of coverage,
    // shared by the lanes projection and by editing.
    [[nodiscard]] const blokkily::Clip* clipAt(int track, int bar) const;

    blokkily::Song song_;
    int current_pattern_ = 0;
    int selected_track_ = 0;
    std::vector<float> peaks_;
    float master_peak_ = 0.0F;
};
