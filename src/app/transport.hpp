#pragma once

#include "pattern_model.hpp"

#include "blokkily/model/timebase.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cmath>
#include <cstdint>

// Drives the playhead every editor shares. Playback advances from a monotonic
// clock while playing; `locate` moves it deterministically without one. The
// playhead is a tick of the song; bars, beats and the tempo shown are read
// from the song's tempo and meter maps, which the controller hands over with
// setTimebase(). The tempo is read-only here: it belongs to the song.
class Transport final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    // The tempo at the playhead.
    Q_PROPERTY(double bpm READ bpm NOTIFY changed)
    Q_PROPERTY(int step READ step NOTIFY changed)
    Q_PROPERTY(double stepFraction READ stepFraction NOTIFY changed)
    Q_PROPERTY(QString position READ position NOTIFY changed)
    Q_PROPERTY(int bar READ bar NOTIFY changed)
    Q_PROPERTY(int bars READ bars NOTIFY changed)

public:
    // A step is a sixteenth note, whatever the meter.
    static constexpr blokkily::Tick ticks_per_step = PatternModel::ticks_per_step;

    explicit Transport(QObject* parent = nullptr);
    bool playing() const noexcept { return playing_; }
    double bpm() const;
    // The step of the bar the playhead is in.
    int step() const;
    // The playhead in sixteenth steps from the start of the song.
    double stepFraction() const noexcept { return tick_ / static_cast<double>(ticks_per_step); }
    // The playhead as a tick of the song.
    double tick() const noexcept { return tick_; }
    // Where the song playhead sits in the arrangement, from the meter map.
    int bar() const;
    int bars() const noexcept { return bars_; }
    // Bar.beat.sixteenth, 1-based, counted in the meter of the bar.
    QString position() const;
    void setSongBars(int bars);
    // The song's tempo and meter, and the resolution its ticks are counted at.
    void setTimebase(const blokkily::TempoMap& tempo, const blokkily::MeterMap& meter,
                     blokkily::Tick ticks_per_beat);
    const blokkily::TempoMap& tempo() const noexcept { return tempo_; }
    const blokkily::MeterMap& meter() const noexcept { return meter_; }
    // Slaves the playhead to the audio engine, so what is drawn is where the
    // song actually is rather than a second clock running alongside it. The
    // sample is converted through the tempo map at `sample_rate`.
    void followSamples(std::uint64_t samples, double sample_rate);
    void releaseFollowing();

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void rewind();
    // Moves the playhead to an absolute sixteenth step of the song.
    Q_INVOKABLE void locate(double step);
    // Moves the playhead to a tick of the song, wrapping at the song's end.
    void locateTick(double tick);

signals:
    void changed();

private:
    void advance();
    // The tick the playhead is in, with a hair of tolerance so a position that
    // a conversion put exactly on a tick is not read as the tick before.
    [[nodiscard]] blokkily::Tick whole_tick() const noexcept {
        return static_cast<blokkily::Tick>(std::floor(tick_ + 1e-6));
    }
    // The end of the last bar the song shows, where the playhead wraps.
    [[nodiscard]] double wrapTick() const;

    QTimer timer_;
    QElapsedTimer clock_;
    bool playing_ = false;
    bool following_ = false;
    int bars_ = 8;
    blokkily::TempoMap tempo_;
    blokkily::MeterMap meter_;
    blokkily::Tick ticks_per_beat_ = 480;
    // The engine's clock, rebuilt when the rate or the timebase changes.
    blokkily::TickClock follow_clock_;
    double follow_rate_ = 0.0;
    double tick_ = 0.0;
};
