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

} // namespace blokkily
