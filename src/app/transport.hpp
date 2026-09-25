#pragma once

#include "pattern_model.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>

#include <cstdint>

// Drives the playhead every editor shares. Playback advances from a monotonic
// clock while playing; `locate` moves it deterministically without one.
class Transport final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(double bpm READ bpm WRITE setBpm NOTIFY changed)
    Q_PROPERTY(int step READ step NOTIFY changed)
    Q_PROPERTY(double stepFraction READ stepFraction NOTIFY changed)
    Q_PROPERTY(QString position READ position NOTIFY changed)
    Q_PROPERTY(int bar READ bar NOTIFY changed)
    Q_PROPERTY(int bars READ bars NOTIFY changed)

public:
    // One bar is sixteen steps: the step grid is a bar of sixteenth notes.
    static constexpr int steps_per_bar = PatternModel::step_count;

    explicit Transport(QObject* parent = nullptr);
    bool playing() const noexcept { return playing_; }
    double bpm() const noexcept { return bpm_; }
    // Where the editors' shared playhead sits inside the bar they show.
    int step() const noexcept { return static_cast<int>(step_position_) % steps_per_bar; }
    double stepFraction() const noexcept { return step_position_; }
    // Where the song playhead sits in the arrangement.
    int bar() const noexcept { return static_cast<int>(step_position_) / steps_per_bar; }
    int bars() const noexcept { return bars_; }
    QString position() const;
    void setSongBars(int bars);
    // Slaves the playhead to the audio engine, so what is drawn is where the
    // song actually is rather than a second clock running alongside it.
    void followSamples(std::uint64_t samples, double sample_rate);
    void releaseFollowing();

    Q_INVOKABLE void play();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void rewind();
    Q_INVOKABLE void locate(double step);
    void setBpm(double bpm);

signals:
    void changed();

private:
    void tick();

    QTimer timer_;
    QElapsedTimer clock_;
    bool playing_ = false;
    bool following_ = false;
    int bars_ = 8;
    double bpm_ = 120.0;
    double step_position_ = 0.0;
};
