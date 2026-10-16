// Following an external MIDI clock (features/midi_clock_input.feature): the
// transport moves as the clock says, and the song plays at the clock's tempo
// through the engine's tempo override, recompiled without a rebuild. The
// song's own tempo map is never edited.

#include "app_controller.hpp"

#include <cmath>

void AppController::setFollowClock(bool on) {
    if (follow_clock_ == on) return;
    follow_clock_ = on;
    clock_follower_.reset();
    // What arrived before is not followed.
    blokkily::ClockEvent stale;
    while (midi_input_->next_clock_event(stale)) {}
    if (!on && followed_bpm_) {
        followed_bpm_.reset();
        applyTempoOverride();
    }
    emit clockChanged();
}

void AppController::applyTempoOverride() {
    if (engine_ && engine_->tempo_override() != tempoOverride()) {
        engine_->set_tempo_override(tempoOverride());
        (void)refreshArrangement();
    }
    syncTimebase();
}

QString AppController::clockStatus() const {
    if (!follow_clock_) return {};
    return followed_bpm_ ? QStringLiteral("%1 BPM").arg(*followed_bpm_, 0, 'f', 1)
                         : QStringLiteral("WAITING");
}

void AppController::serveClock() {
    blokkily::ClockEvent event;
    if (!follow_clock_) {
        while (midi_input_->next_clock_event(event)) {}
        return;
    }
    std::uint64_t latest = 0;
    bool moved = false;
    while (midi_input_->next_clock_event(event)) {
        latest = event.time_ns;
        switch (clock_follower_.feed(event)) {
        case blokkily::ClockFollower::Move::start:
            seekToTick(0.0);
            if (transport_ != nullptr && !transport_->playing()) togglePlayback();
            moved = true;
            break;
        case blokkily::ClockFollower::Move::resume:
            if (transport_ != nullptr && !transport_->playing()) togglePlayback();
            moved = true;
            break;
        case blokkily::ClockFollower::Move::stop:
            if (transport_ != nullptr && transport_->playing()) togglePlayback();
            moved = true;
            break;
        case blokkily::ClockFollower::Move::seek:
            seekToStep(static_cast<double>(clock_follower_.position()));
            moved = true;
            break;
        case blokkily::ClockFollower::Move::none: break;
        }
    }
    // The tempo, once a beat of ticks says it, followed when it moves by
    // more than a twentieth of a BPM: the jitter of a clock arriving over
    // USB is not a tempo change.
    const auto bpm = clock_follower_.bpm(latest != 0 ? latest : blokkily::monotonic_ns());
    if (bpm && (!followed_bpm_ || std::abs(*bpm - *followed_bpm_) > 0.05)) {
        followed_bpm_ = std::round(*bpm * 100.0) / 100.0;
        applyTempoOverride();
        moved = true;
    }
    if (moved) emit clockChanged();
}
