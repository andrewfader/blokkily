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
    clock_lock_.reset(0.0);
    locked_bpm_.reset();
    // What arrived before is not followed.
    blokkily::ClockEvent stale;
    while (midi_input_->next_clock_event(stale)) {}
    if (!on && followed_bpm_) {
        followed_bpm_.reset();
        locked_bpm_.reset();
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
    // Eight beats of ticks measure the tempo far more steadily than one.
    const auto heard = clock_lock_.bpm() ? clock_lock_.bpm() : followed_bpm_;
    return heard ? QStringLiteral("%1 BPM").arg(*heard, 0, 'f', 1) : QStringLiteral("WAITING");
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
        const auto move = clock_follower_.feed(event);
        if (event.kind == blokkily::ClockEvent::Kind::tick) lockTick(event.time_ns);
        switch (move) {
        case blokkily::ClockFollower::Move::start:
            seekToTick(0.0);
            clock_lock_.reset(0.0);
            if (transport_ != nullptr && !transport_->playing()) togglePlayback();
            moved = true;
            break;
        case blokkily::ClockFollower::Move::resume:
            clock_lock_.resume();
            if (transport_ != nullptr && !transport_->playing()) togglePlayback();
            moved = true;
            break;
        case blokkily::ClockFollower::Move::stop:
            if (transport_ != nullptr && transport_->playing()) togglePlayback();
            moved = true;
            break;
        case blokkily::ClockFollower::Move::seek:
            seekToStep(static_cast<double>(clock_follower_.position()));
            clock_lock_.reset(static_cast<double>(clock_follower_.position()) / 4.0);
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
        moved = true;
    }
    // The song plays at the clock's tempo, trimmed by the phase lock once it
    // has settled, so it stays on the clock's beat (ClockLock). A trim is
    // applied at most every half beat of ticks: each is a recompile.
    if (followed_bpm_) {
        const double wanted = clock_lock_.locked() ? clock_lock_.tempo(*followed_bpm_)
                                                   : clock_lock_.bpm().value_or(*followed_bpm_);
        const bool due = !locked_bpm_ || ticks_since_trim_ >= 12 || moved;
        if (due && (!locked_bpm_ || std::abs(wanted - *locked_bpm_) > 0.002)) {
            locked_bpm_ = std::round(wanted * 1000.0) / 1000.0;
            ticks_since_trim_ = 0;
            applyTempoOverride();
        }
    } else if (locked_bpm_) {
        locked_bpm_.reset();
        applyTempoOverride();
    }
    const auto shown = clockStatus();
    if (moved || shown != shown_clock_status_) {
        shown_clock_status_ = shown;
        emit clockChanged();
    }
}

// Where the song was heard when a tick arrived, against where the tick says
// the clock's song was (ClockLock): from the engine's heard reference,
// carried forward to the tick's time along the song's own tick clock.
void AppController::lockTick(std::uint64_t time_ns) {
    ++ticks_since_trim_;
    std::optional<double> song_beat;
    const bool rolling = engine_ && clock_follower_.running() && transport_ != nullptr &&
                         transport_->playing();
    const auto reference = rolling ? engine_->heard_reference() : std::nullopt;
    const double per_beat =
        static_cast<double>(std::max<blokkily::Tick>(1, song_->song().ticks_per_beat()));
    if (reference) {
        const auto& clock = engine_->published_clock();
        const double seconds =
            (static_cast<double>(time_ns) - static_cast<double>(reference->ns)) * 1e-9;
        double beat = clock.tick_at(clock.sample_at_tick(reference->tick) +
                                    seconds * clock.sample_rate()) / per_beat;
        // A song that loops starts again under a clock that does not: the
        // beat is read as the lap nearest the clock's.
        const auto length = engine_->song_samples();
        const double lap = length > 0 ? clock.tick_at(static_cast<double>(length)) / per_beat : 0.0;
        if (lap > 0.0) beat += lap * std::round((clock_lock_.next_beat() - beat) / lap);
        song_beat = beat;
    }
    const bool was_locked = clock_lock_.locked();
    clock_lock_.tick(time_ns, song_beat);
    // Settled for the first time a long way off the clock (the song started
    // late, or was moved): one jump puts it on the beat, then the trims
    // keep it there.
    if (!was_locked && clock_lock_.locked() && std::abs(clock_lock_.error()) > 0.05) {
        const double lap_ticks = engine_->song_samples() > 0
            ? engine_->published_clock().tick_at(static_cast<double>(engine_->song_samples())) : 0.0;
        double target = engine_->published_clock().tick_at(static_cast<double>(engine_->sample_position())) +
                        clock_lock_.error() * per_beat;
        if (lap_ticks > 0.0) target = std::fmod(std::fmod(target, lap_ticks) + lap_ticks, lap_ticks);
        seekToTick(target);
        clock_lock_.aligned();
    }
}
