#include "transport.hpp"

#include <QtGlobal>

#include <cmath>

Transport::Transport(QObject* parent) : QObject(parent) {
    timer_.setInterval(16);
    QObject::connect(&timer_, &QTimer::timeout, this, &Transport::tick);
}

// Bar.beat.sixteenth across the whole arrangement, which is what a transport
// readout means. A step is a sixteenth note, so sixteen of them make a bar.
QString Transport::position() const {
    const auto absolute = static_cast<int>(step_position_);
    const int step = absolute % steps_per_bar;
    return QString("%1.%2.%3")
        .arg(absolute / steps_per_bar + 1).arg(step / 4 + 1).arg(step % 4 + 1);
}

void Transport::setSongBars(int bars) {
    bars_ = std::max(1, bars);
    locate(step_position_);
}

void Transport::followSamples(std::uint64_t samples, double sample_rate) {
    following_ = true;
    if (sample_rate <= 0.0 || bpm_ <= 0.0) return;
    // Four sixteenths to the beat.
    const double samples_per_step = sample_rate * 60.0 / (bpm_ * 4.0);
    if (samples_per_step <= 0.0) return;
    locate(static_cast<double>(samples) / samples_per_step);
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
    step_position_ = 0.0;
    clock_.restart();
    emit changed();
}

void Transport::locate(double step) {
    const double length = steps_per_bar * bars_;
    step_position_ = std::fmod(std::fmod(step, length) + length, length);
    clock_.restart();
    emit changed();
}

void Transport::setBpm(double bpm) {
    bpm_ = qBound(20.0, bpm, 300.0);
    emit changed();
}

void Transport::tick() {
    // While an engine is bound the playhead follows its sample position; this
    // free-running clock only drives the interface when nothing is rendering.
    if (following_) return;
    // A 16th note per step: one beat covers four steps.
    const double elapsed = static_cast<double>(clock_.restart()) / 1000.0;
    locate(step_position_ + elapsed * bpm_ / 60.0 * 4.0);
}
