// SongEngine's metronome and count-in (item 3.7, decision 14). The click
// itself is in engine/engine_metronome.cpp; this is the engine's side of it:
// the controls the control thread sets, and the render callback's start and
// end of a count-in.

#include "blokkily/audio/song_engine.hpp"

#include "engine/arrangement.hpp"
#include "engine/engine_metronome.hpp"

#include "blokkily/audio/mixer.hpp"
#include "blokkily/model/metronome.hpp"

#include <algorithm>

namespace blokkily {

void SongEngine::set_playing(bool playing) noexcept {
    if (!playing) {
        stop_requested_.store(true, std::memory_order_release);
        // A count-in asked for and not yet begun is not begun later.
        metronome_->count_in_request.store(0, std::memory_order_release);
    }
    playing_.store(playing, std::memory_order_release);
}

void SongEngine::set_metronome(bool enabled, double level_db) noexcept {
    metronome_->enabled.store(enabled, std::memory_order_relaxed);
    metronome_->gain.store(
        static_cast<float>(db_to_linear(MetronomeSettings::clamp_level(level_db))),
        std::memory_order_relaxed);
}

void SongEngine::set_metronome_enabled(bool enabled) noexcept {
    metronome_->enabled.store(enabled, std::memory_order_relaxed);
}

bool SongEngine::metronome_enabled() const noexcept {
    return metronome_->enabled.load(std::memory_order_relaxed);
}

void SongEngine::play_with_count_in(std::uint32_t bars) noexcept {
    const auto counted = std::min<std::uint32_t>(
        bars, static_cast<std::uint32_t>(MetronomeSettings::maximum_count_in_bars));
    // The request goes first, so the block that sees the transport playing
    // also sees the count-in it starts with.
    metronome_->count_in_request.store(counted, std::memory_order_release);
    playing_.store(true, std::memory_order_release);
}

bool SongEngine::counting_in() const noexcept {
    return metronome_->counting_in.load(std::memory_order_acquire);
}

void SongEngine::start_count_in(std::uint32_t bars) noexcept {
    if (live_ == nullptr || song_samples_ == 0 || sample_rate_ <= 0.0) return;
    // The count-in is in the tempo and the meter of the place the song will
    // start from: N bars of that bar's beats at the tempo there.
    const auto resting = sample_position_ % song_samples_;
    const auto& clock = live_->clock;
    const double bpm = clock.bpm_at_sample(static_cast<double>(resting));
    const Tick tick = tick_at_sample(clock, resting);
    const auto& meter = live_->meter.meter_in(live_->meter.bar_at(tick));
    const auto beat_ticks = static_cast<double>(
        whole_note_ticks / std::max<Tick>(1, static_cast<Tick>(meter.denominator)));
    const auto ticks_per_beat = static_cast<double>(std::max<Tick>(1, live_->ticks_per_beat));
    const double beat_samples = beat_ticks * sample_rate_ * 60.0 / (bpm * ticks_per_beat);
    engine::begin_count_in(*metronome_, bars,
                           static_cast<std::uint32_t>(std::max<std::int16_t>(1, meter.numerator)),
                           beat_samples, output_latency());
}

void SongEngine::finish_count_in() noexcept {
    auto& metronome = *metronome_;
    metronome.song_waiting = false;
    metronome.counting_in.store(false, std::memory_order_release);
    // A note played into the count-in and still held is part of the take: it
    // is recorded as starting where the song starts. What was played and let
    // go during the count-in is not.
    if (recording_.load(std::memory_order_acquire) && live_ != nullptr && song_samples_ > 0) {
        const auto start = sample_position_ % song_samples_;
        const Tick tick = tick_at_sample(live_->clock, start);
        constexpr std::size_t keys = 128;
        for (std::size_t track = 0; track < metronome.held_tracks; ++track)
            for (std::size_t key = 0; key < keys; ++key) {
                const auto& held = metronome.held[track * keys + key];
                if (!held.held) continue;
                (void)captured_.push({static_cast<std::uint32_t>(track), start, tick,
                                      {PluginEvent::Type::note_on, 0,
                                       static_cast<std::int32_t>(key), held.velocity,
                                       held.cents}});
            }
    }
    std::fill(metronome.held.begin(), metronome.held.end(), engine::HeldNote{});
}

} // namespace blokkily
