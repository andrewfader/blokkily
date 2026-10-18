#include "blokkily/midi/clock_follower.hpp"

#include <algorithm>

namespace blokkily {

ClockFollower::Move ClockFollower::feed(const ClockEvent& event) noexcept {
    switch (event.kind) {
    case ClockEvent::Kind::tick:
        if (last_tick_ != 0 && event.time_ns > last_tick_) {
            const auto interval = event.time_ns - last_tick_;
            // A gap of more than a second is the clock pausing, not a tempo
            // of 2.5 BPM: it starts the measurement again.
            if (interval > 1'000'000'000ULL) {
                count_ = 0;
                next_ = 0;
            } else {
                intervals_[next_] = interval;
                next_ = (next_ + 1) % window;
                count_ = std::min(count_ + 1, window);
            }
        }
        last_tick_ = event.time_ns;
        return Move::none;
    case ClockEvent::Kind::start:
        running_ = true;
        position_ = 0;
        return Move::start;
    case ClockEvent::Kind::stop:
        running_ = false;
        return Move::stop;
    case ClockEvent::Kind::resume:
        running_ = true;
        return Move::resume;
    case ClockEvent::Kind::position:
        position_ = event.sixteenths;
        return Move::seek;
    }
    return Move::none;
}

std::optional<double> ClockFollower::bpm(std::uint64_t now_ns) const noexcept {
    if (count_ < window || last_tick_ == 0) return std::nullopt;
    if (now_ns > last_tick_ && now_ns - last_tick_ > 2'000'000'000ULL) return std::nullopt;
    // The mean of the middle half: a clock's jitter averages out, and a late
    // or early tick or two falls outside it.
    auto sorted = intervals_;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0.0;
    for (std::size_t i = window / 4; i < window - window / 4; ++i) sum += static_cast<double>(sorted[i]);
    const double interval = sum / static_cast<double>(window - 2 * (window / 4));
    if (interval <= 0.0) return std::nullopt;
    // 24 ticks a beat: a beat lasts 24 intervals.
    return 60.0e9 / (interval * 24.0);
}

void ClockFollower::reset() noexcept {
    count_ = 0;
    next_ = 0;
    last_tick_ = 0;
    running_ = false;
    position_ = 0;
}

void ClockLock::reset(double beat) noexcept {
    beat_ = beat;
    resume();
}

void ClockLock::resume() noexcept {
    error_ = 0.0;
    measured_ = 0;
    count_ = 0;
    next_ = 0;
}

void ClockLock::aligned() noexcept {
    error_ = 0.0;
    measured_ = 0;
}

void ClockLock::tick(std::uint64_t time_ns, std::optional<double> song_beat) noexcept {
    times_[next_] = time_ns;
    beats_[next_] = beat_;
    next_ = (next_ + 1) % history;
    count_ = std::min(count_ + 1, history);
    if (song_beat) {
        const double raw = beat_ - *song_beat;
        // The first measured ticks set the average outright; then each moves
        // it a twelfth of the way, half a beat's worth of smoothing.
        ++measured_;
        error_ = measured_ == 1 ? raw : error_ + (raw - error_) / static_cast<double>(settle_ticks);
    }
    beat_ += 1.0 / 24.0;
}

std::optional<double> ClockLock::bpm() const noexcept {
    if (count_ < 4 * 24) return std::nullopt;
    // Least squares of beat against time, in seconds from the oldest tick.
    const auto oldest = times_[(next_ + history - count_) % history];
    double mean_t = 0.0, mean_b = 0.0;
    for (std::size_t index = 0; index < count_; ++index) {
        mean_t += static_cast<double>(times_[index] - oldest) * 1e-9;
        mean_b += beats_[index];
    }
    mean_t /= static_cast<double>(count_);
    mean_b /= static_cast<double>(count_);
    double spread = 0.0, together = 0.0;
    for (std::size_t index = 0; index < count_; ++index) {
        const double t = static_cast<double>(times_[index] - oldest) * 1e-9 - mean_t;
        spread += t * t;
        together += t * (beats_[index] - mean_b);
    }
    if (!(spread > 0.0)) return std::nullopt;
    return together / spread * 60.0;
}

double ClockLock::tempo(double fallback) const noexcept {
    const double trim = std::clamp(error_ / horizon_beats, -largest_trim, largest_trim);
    return bpm().value_or(fallback) * (1.0 + trim);
}

} // namespace blokkily
