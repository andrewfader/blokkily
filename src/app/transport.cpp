#include "transport.hpp"

#include <QtGlobal>

#include <cmath>

Transport::Transport(QObject* parent) : QObject(parent) {
    timer_.setInterval(16);
    QObject::connect(&timer_, &QTimer::timeout, this, &Transport::advance);
}

double Transport::bpm() const {
    return tempo_.bpm_at(whole_tick());
}

int Transport::bar() const {
    return static_cast<int>(meter_.bar_at(whole_tick()));
}

int Transport::step() const {
    const auto at = whole_tick();
    const auto into = at - meter_.bar_start(meter_.bar_at(at));
    return static_cast<int>(into / ticks_per_step);
}

// Bar.beat.sixteenth across the whole arrangement, which is what a transport
// readout means, counted in the meter of the bar: in 7/8 a beat is an eighth.
QString Transport::position() const {
    const auto where = meter_.position_at(whole_tick());
    return QString("%1.%2.%3").arg(where.bar + 1).arg(where.beat + 1).arg(where.sixteenth + 1);
}

double Transport::wrapTick() const {
    return static_cast<double>(meter_.bar_start(std::max(1, bars_)));
}

void Transport::setSongBars(int bars) {
    bars_ = std::max(1, bars);
    locateTick(tick_);
}

void Transport::setTimebase(const blokkily::TempoMap& tempo, const blokkily::MeterMap& meter,
                            blokkily::Tick ticks_per_beat) {
    const auto resolution = std::max<blokkily::Tick>(1, ticks_per_beat);
    if (tempo == tempo_ && meter == meter_ && resolution == ticks_per_beat_) return;
    tempo_ = tempo.valid() ? tempo : blokkily::TempoMap{};
    meter_ = meter.valid() ? meter : blokkily::MeterMap{};
    ticks_per_beat_ = resolution;
    follow_rate_ = 0.0;   // the engine's clock is rebuilt on the next follow
    // The playhead keeps its tick: a new tempo moves where the tick falls in
    // time, not where it is in the music.
    locateTick(tick_);
}

void Transport::followSamples(std::uint64_t samples, double sample_rate) {
    following_ = true;
    if (!(sample_rate > 0.0)) return;
    if (sample_rate != follow_rate_) {
        follow_clock_ = blokkily::TickClock(tempo_, ticks_per_beat_, sample_rate);
        follow_rate_ = sample_rate;
    }
    locateTick(follow_clock_.tick_at(static_cast<double>(samples)));
}

void Transport::releaseFollowing() { following_ = false; }

void Transport::play() {
    if (playing_) return;
    playing_ = true;
    clock_.restart();
    timer_.start();
    emit changed();
}

void Transport::stop() {
    if (!playing_) return;
    playing_ = false;
    timer_.stop();
    emit changed();
}

void Transport::toggle() { playing_ ? stop() : play(); }

void Transport::rewind() {
    tick_ = 0.0;
    clock_.restart();
    emit changed();
}

void Transport::locate(double step) { locateTick(step * static_cast<double>(ticks_per_step)); }

void Transport::locateTick(double tick) {
    const double length = wrapTick();
    tick_ = length > 0.0 ? std::fmod(std::fmod(tick, length) + length, length) : 0.0;
    clock_.restart();
    emit changed();
}

void Transport::advance() {
    // While an engine is bound the playhead follows its sample position; this
    // free-running clock only drives the interface when nothing is rendering,
    // and it runs through the song's tempo map as the engine would.
    if (following_) return;
    const double elapsed = static_cast<double>(clock_.restart()) / 1000.0;
    const double seconds = tempo_.seconds_at_tick(tick_, ticks_per_beat_) + elapsed;
    locateTick(tempo_.tick_at_seconds(seconds, ticks_per_beat_));
}
