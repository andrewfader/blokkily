// Arrangement loop: wrap samples the control thread publishes, and the
// playhead fold the render callback uses. process() itself still lives in
// song_engine.cpp so the wrap sits on the same path as the song-end wrap.

#include "blokkily/audio/song_engine.hpp"

#include "blokkily/model/loop.hpp"
#include "blokkily/model/timebase.hpp"

namespace blokkily {

void SongEngine::set_loop(bool enabled, std::uint64_t start_sample,
                          std::uint64_t end_sample) noexcept {
    loop_start_samples_.store(start_sample, std::memory_order_relaxed);
    loop_end_samples_.store(end_sample, std::memory_order_relaxed);
    loop_enabled_.store(enabled, std::memory_order_release);
}

void SongEngine::set_punch(bool enabled, std::uint64_t start_sample,
                           std::uint64_t end_sample) noexcept {
    punch_start_samples_.store(start_sample, std::memory_order_relaxed);
    punch_end_samples_.store(end_sample, std::memory_order_relaxed);
    punch_enabled_.store(enabled && end_sample > start_sample, std::memory_order_release);
}

void SongEngine::set_loop_enabled(bool enabled) noexcept {
    loop_enabled_.store(enabled, std::memory_order_release);
}

bool SongEngine::loop_enabled() const noexcept {
    return loop_enabled_.load(std::memory_order_acquire);
}

std::uint64_t SongEngine::wrap_end_sample() const noexcept {
    if (song_samples_ == 0) return 0;
    if (!loop_enabled_.load(std::memory_order_acquire)) return song_samples_;
    const auto end = loop_end_samples_.load(std::memory_order_relaxed);
    const auto start = loop_start_samples_.load(std::memory_order_relaxed);
    if (end > start && end <= song_samples_) return end;
    return song_samples_;
}

std::uint64_t SongEngine::wrap_start_sample() const noexcept {
    if (!loop_enabled_.load(std::memory_order_acquire)) return 0;
    const auto end = loop_end_samples_.load(std::memory_order_relaxed);
    const auto start = loop_start_samples_.load(std::memory_order_relaxed);
    if (end > start && end <= song_samples_ && start < song_samples_) return start;
    return 0;
}

void SongEngine::fold_playhead() noexcept {
    if (!loop_enabled_.load(std::memory_order_acquire)) return;
    // Played with the loop off, the playhead counts on past the song's end
    // (the song wraps by the modulo): turned on, the loop starts from where
    // the song has come round to, not from that count. (Exactly at the
    // song's end is a loop that ends there, folded below.)
    if (song_samples_ != 0 && sample_position_ > song_samples_) {
        sample_position_ %= song_samples_;
        cursors_valid_ = false;
    }
    const auto end = wrap_end_sample();
    if (end == 0 || sample_position_ < end) return;
    const auto start = wrap_start_sample();
    const auto length = end - start;
    if (length == 0) {
        sample_position_ = 0;
        cursors_valid_ = false;
        return;
    }
    sample_position_ = start + (sample_position_ - end) % length;
    cursors_valid_ = false;
    ++laps_;
}

void SongEngine::apply_loop(const Song& song) {
    if (song.loop.punching())
        set_punch(true, sample_for_tick(published_clock_, static_cast<double>(song.loop.start)),
                  sample_for_tick(published_clock_, static_cast<double>(song.loop.end)));
    else
        set_punch(false, 0, 0);
    // The length recompile() has just published: the callback may not have
    // taken the new arrangement yet, and song_samples_ is the callback's.
    const auto song_samples = published_song_samples_.load(std::memory_order_acquire);
    if (!song.loop.active() || song_samples == 0) {
        set_loop(false, 0, 0);
        return;
    }
    auto start = sample_for_tick(published_clock_, static_cast<double>(song.loop.start));
    auto end = sample_for_tick(published_clock_, static_cast<double>(song.loop.end));
    if (end > song_samples) end = song_samples;
    if (start >= end) {
        set_loop(false, 0, 0);
        return;
    }
    set_loop(true, start, end);
}

} // namespace blokkily
