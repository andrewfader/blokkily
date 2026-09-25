#include "engine_metronome.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace blokkily::engine {

namespace {

// Keys a track can hold.
constexpr std::size_t keys = 128;

// The click sounds: a short cosine burst that starts at its loudest, so the
// first sample of a click is the sample its beat falls on, and dies away
// inside 30 ms. The downbeat is higher and twice as loud as the other beats.
constexpr double click_seconds = 0.030;
constexpr double click_decay_seconds = 0.008;
constexpr double accent_hz = 1600.0;
constexpr double beat_hz = 1000.0;
constexpr float accent_level = 1.0F;
constexpr float beat_level = 0.5F;

std::vector<float> click_sound(double sample_rate, double frequency, float level) {
    const auto length = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::llround(sample_rate * click_seconds)));
    std::vector<float> sound(length, 0.0F);
    for (std::size_t index = 0; index < length; ++index) {
        const double seconds = static_cast<double>(index) / sample_rate;
        // An exponential decay that is brought to zero at the end, so the
        // click never stops on a step.
        const double envelope = std::exp(-seconds / click_decay_seconds) *
                                (1.0 - static_cast<double>(index) / static_cast<double>(length));
        sound[index] = static_cast<float>(
            level * envelope * std::cos(2.0 * std::numbers::pi * frequency * seconds));
    }
    return sound;
}

// Adds what is left of the sounding click to frames [from, to) of `output`.
void render_voice(MetronomePlayback& metronome, StereoBlock output, std::size_t from,
                  std::size_t to, float gain) noexcept {
    for (std::size_t frame = from; frame < to && metronome.voice != nullptr; ++frame) {
        const float value = metronome.voice[metronome.voice_at] * gain;
        output.left[frame] += value;
        output.right[frame] += value;
        if (++metronome.voice_at >= metronome.voice_length) metronome.voice = nullptr;
    }
}

void start_voice(MetronomePlayback& metronome, bool accent) noexcept {
    const auto& sound = accent ? metronome.accent : metronome.beat;
    if (sound.empty()) {
        metronome.voice = nullptr;
        return;
    }
    metronome.voice = sound.data();
    metronome.voice_length = sound.size();
    metronome.voice_at = 0;
}

void cancel_held(MetronomePlayback& metronome) noexcept {
    std::fill(metronome.held.begin(), metronome.held.end(), HeldNote{});
}

} // namespace

void compile_clicks(ArrangementClicks& target, const MeterMap& meter, const TickClock& clock,
                    Tick song_length, std::uint64_t song_samples) {
    target.clicks.clear();
    for (std::int32_t bar = 0;; ++bar) {
        const Tick start = meter.bar_start(bar);
        if (start >= song_length || start < 0) break;
        const auto& change = meter.meter_in(bar);
        // The meter's beat unit: a quarter in 4/4, an eighth in 7/8.
        const Tick beat = std::max<Tick>(1, whole_note_ticks / std::max<Tick>(1, change.denominator));
        for (std::int32_t index = 0; index < change.numerator; ++index) {
            const Tick at = start + index * beat;
            if (at >= song_length) break;
            // The rule every note is placed by, so a click and a note on the
            // same tick start on the same sample.
            const auto sample = sample_for_tick(clock, static_cast<double>(at));
            if (sample >= song_samples) break;
            target.clicks.push_back({sample, index == 0});
        }
    }
}

void prepare_metronome(MetronomePlayback& metronome, double sample_rate, std::size_t tracks) {
    metronome.accent = click_sound(sample_rate, accent_hz, accent_level);
    metronome.beat = click_sound(sample_rate, beat_hz, beat_level);
    metronome.held.assign(tracks * keys, HeldNote{});
    metronome.held_tracks = tracks;
    reset_metronome(metronome);
}

void reset_metronome(MetronomePlayback& metronome) noexcept {
    metronome.voice = nullptr;
    metronome.voice_length = 0;
    metronome.voice_at = 0;
    metronome.rolled = 0;
    metronome.was_rolling = false;
    metronome.count_clicking = false;
    metronome.song_waiting = false;
    metronome.count_elapsed = 0;
    metronome.next_count_beat = 0;
    metronome.count_beats = 0;
    metronome.count_in_request.store(0, std::memory_order_release);
    metronome.counting_in.store(false, std::memory_order_release);
    cancel_held(metronome);
}

void begin_count_in(MetronomePlayback& metronome, std::uint32_t bars,
                    std::uint32_t beats_per_bar, double beat_samples,
                    std::uint32_t latency) noexcept {
    cancel_held(metronome);
    metronome.count_beats_per_bar = std::max<std::uint32_t>(1, beats_per_bar);
    metronome.count_beats = bars * metronome.count_beats_per_bar;
    metronome.count_beat = std::isfinite(beat_samples) && beat_samples > 0.0 ? beat_samples : 0.0;
    metronome.count_elapsed = 0;
    metronome.next_count_beat = 0;
    const double length = static_cast<double>(metronome.count_beats) * metronome.count_beat;
    const auto total = static_cast<std::uint64_t>(std::llround(length));
    // The song is rendered `latency` samples before it is heard, so it starts
    // that much before the count-in ends and its first beat sounds right after
    // the last count-in bar.
    metronome.song_starts_at = total - std::min<std::uint64_t>(total, latency);
    metronome.count_clicking = metronome.count_beats > 0 && total > 0;
    metronome.song_waiting = metronome.song_starts_at > 0;
    metronome.was_rolling = false;
    metronome.counting_in.store(metronome.song_waiting, std::memory_order_release);
}

std::uint64_t count_in_remaining(const MetronomePlayback& metronome) noexcept {
    if (!metronome.song_waiting || metronome.count_elapsed >= metronome.song_starts_at) return 0;
    return metronome.song_starts_at - metronome.count_elapsed;
}

void hold_count_in_notes(MetronomePlayback& metronome,
                         std::span<const RoutedEvent> incoming) noexcept {
    for (const auto& routed : incoming) {
        const auto key = routed.event.key_or_parameter;
        if (routed.track >= metronome.held_tracks || key < 0 ||
            key >= static_cast<std::int32_t>(keys))
            continue;
        auto& held = metronome.held[routed.track * keys + static_cast<std::size_t>(key)];
        if (routed.event.type == PluginEvent::Type::note_on && routed.event.value > 0.0)
            held = {true, routed.event.value, routed.event.cents};
        else if (routed.event.type == PluginEvent::Type::note_on ||
                 routed.event.type == PluginEvent::Type::note_off)
            held = HeldNote{};
    }
}

void render_clicks(MetronomePlayback& metronome, StereoBlock output,
                   const SongClicks& song) noexcept {
    const auto frames = std::min(output.left.size(), output.right.size());
    const float gain = metronome.gain.load(std::memory_order_relaxed);
    const bool song_on = song.rolling && song.clicks != nullptr && !song.clicks->clicks.empty() &&
                         song.song_samples > 0 &&
                         metronome.enabled.load(std::memory_order_relaxed);
    const bool counting = metronome.count_clicking || metronome.song_waiting;
    std::size_t at = 0;
    // The first frames after the song starts still carry the compensation,
    // not the song, and have no beat of their own.
    std::size_t song_from = 0;
    if (song_on && metronome.rolled < song.latency)
        song_from = static_cast<std::size_t>(
            std::min<std::uint64_t>(frames, song.latency - metronome.rolled));
    while (at < frames) {
        std::size_t next = frames;
        bool accent = false;
        bool from_count = false;
        if (metronome.count_clicking && metronome.next_count_beat < metronome.count_beats) {
            const auto due = static_cast<std::uint64_t>(
                std::llround(static_cast<double>(metronome.next_count_beat) * metronome.count_beat));
            const auto offset = due > metronome.count_elapsed ? due - metronome.count_elapsed : 0;
            if (offset < frames) {
                next = std::max<std::size_t>(at, static_cast<std::size_t>(offset));
                accent = metronome.next_count_beat % metronome.count_beats_per_bar == 0;
                from_count = true;
            }
        }
        if (song_on && song_from < frames) {
            const auto& clicks = song.clicks->clicks;
            const auto heard = (song.heard_start + song_from) % song.song_samples;
            auto found = std::lower_bound(
                clicks.begin(), clicks.end(), heard,
                [](const Click& click, std::uint64_t sample) { return click.sample < sample; });
            std::uint64_t offset = song_from;
            if (found != clicks.end()) {
                offset += found->sample - heard;
            } else {
                // Past the last beat: the next one is the first after the loop.
                offset += song.song_samples - heard + clicks.front().sample;
                found = clicks.begin();
            }
            if (offset < next) {
                next = static_cast<std::size_t>(offset);
                accent = found->accent;
                from_count = false;
            }
        }
        if (next >= frames) break;
        render_voice(metronome, output, at, next, gain);
        start_voice(metronome, accent);
        at = next;
        if (from_count) ++metronome.next_count_beat;
        else song_from = next + 1;
    }
    render_voice(metronome, output, at, frames, gain);
    if (counting) {
        metronome.count_elapsed += frames;
        if (metronome.next_count_beat >= metronome.count_beats) metronome.count_clicking = false;
    }
    if (song.rolling) metronome.rolled += frames;
}

} // namespace blokkily::engine
