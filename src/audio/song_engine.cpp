#include "blokkily/audio/song_engine.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace blokkily {
namespace {
// The most events one track can receive in a single block. Enough for dense
// ratcheted steps at any sane block size, and fixed so the render callback can
// keep them on the stack.
constexpr std::size_t maximum_events_per_block = 256;
} // namespace

SongEngine::SongEngine() = default;
SongEngine::~SongEngine() = default;

void SongEngine::set_instrument(std::size_t track, std::unique_ptr<PluginInstance> instrument) {
    while (tracks_.size() <= track) tracks_.push_back(std::make_unique<TrackPlayback>());
    tracks_[track]->instrument = std::move(instrument);
}

bool SongEngine::has_instrument(std::size_t track) const {
    return track < tracks_.size() && tracks_[track]->instrument != nullptr;
}

bool SongEngine::prepare(const Song& song, double bpm, double sample_rate,
                         std::uint32_t maximum_block_size, std::uint64_t seed,
                         std::string* error) {
    const auto fail = [error](const char* message) {
        if (error != nullptr) *error = message;
        return false;
    };
    if (bpm <= 0.0 || sample_rate <= 0.0 || maximum_block_size == 0)
        return fail("invalid playback settings");
    if (!song.consistent()) return fail("song refers to a track or pattern that does not exist");

    while (tracks_.size() < song.tracks.size()) tracks_.push_back(std::make_unique<TrackPlayback>());
    tracks_.resize(song.tracks.size());
    for (auto& track : tracks_)
        if (!track) track = std::make_unique<TrackPlayback>();

    const double ticks_per_beat = static_cast<double>(song.pattern().ticks_per_beat());
    const double samples_per_tick = sample_rate * 60.0 / (bpm * ticks_per_beat);
    song_samples_ = static_cast<std::uint64_t>(
        std::llround(static_cast<double>(song.length()) * samples_per_tick));
    if (song_samples_ == 0) return fail("song has no length");
    maximum_block_ = maximum_block_size;
    sample_rate_ = sample_rate;

    for (std::size_t index = 0; index < tracks_.size(); ++index) {
        auto& track = *tracks_[index];
        track.timeline = compile_timeline(song.arrange(index, seed), samples_per_tick,
                                          song_samples_ - 1);
        track.cursor = 0;
        track.left.assign(maximum_block_size, 0.0F);
        track.right.assign(maximum_block_size, 0.0F);
        track.peak.store(0.0F, std::memory_order_relaxed);
        if (track.instrument && !track.instrument->activate(sample_rate, 1, maximum_block_size))
            return fail("an instrument refused to activate");
    }
    apply_mix(song);
    sample_position_ = 0;
    cursors_valid_ = false;
    return true;
}

void SongEngine::set_strip(std::size_t track, const MixerStrip& strip, bool any_solo) {
    if (track >= tracks_.size()) return;
    const auto gain = strip_gain(strip, any_solo);
    tracks_[track]->gain_left.store(gain.left, std::memory_order_relaxed);
    tracks_[track]->gain_right.store(gain.right, std::memory_order_relaxed);
}

void SongEngine::apply_mix(const Song& song) {
    const bool solo = song.any_solo();
    for (std::size_t index = 0; index < song.tracks.size() && index < tracks_.size(); ++index)
        set_strip(index, song.tracks[index].mix, solo);
    set_master_gain_db(song.master_gain_db);
}

void SongEngine::set_master_gain_db(double decibels) {
    master_gain_.store(static_cast<float>(db_to_linear(decibels)), std::memory_order_relaxed);
}

float SongEngine::track_peak(std::size_t track) const {
    if (track >= tracks_.size()) return 0.0F;
    return tracks_[track]->peak.load(std::memory_order_relaxed);
}

std::vector<std::byte> SongEngine::save_track_state(std::size_t track) {
    if (!has_instrument(track)) return {};
    return tracks_[track]->instrument->save_state();
}

bool SongEngine::load_track_state(std::size_t track, std::span<const std::byte> state) {
    return has_instrument(track) && tracks_[track]->instrument->load_state(state);
}

void SongEngine::seek_cursors(std::uint64_t position) noexcept {
    for (auto& track : tracks_) {
        const auto found = std::lower_bound(
            track->timeline.begin(), track->timeline.end(), position,
            [](const TimedPluginEvent& event, std::uint64_t sample) {
                return event.sample < sample;
            });
        track->cursor = static_cast<std::size_t>(found - track->timeline.begin());
    }
}

void SongEngine::process_chunk(StereoBlock output, std::uint64_t song_position) noexcept {
    const auto frames = output.left.size();
    std::fill(output.left.begin(), output.left.end(), 0.0F);
    std::fill(output.right.begin(), output.right.end(), 0.0F);
    const auto end = song_position + frames;

    for (auto& handle : tracks_) {
        auto& track = *handle;
        std::array<PluginEvent, maximum_events_per_block> block_events{};
        std::size_t count = 0;
        while (track.cursor < track.timeline.size() &&
               track.timeline[track.cursor].sample < end) {
            const auto& timed = track.timeline[track.cursor];
            if (timed.sample >= song_position && count < block_events.size()) {
                block_events[count] = timed.event;
                block_events[count].sample_offset =
                    static_cast<std::uint32_t>(timed.sample - song_position);
                ++count;
            }
            ++track.cursor;
        }
        if (!track.instrument) {
            track.peak.store(0.0F, std::memory_order_relaxed);
            continue;
        }
        const std::span<float> left{track.left.data(), frames};
        const std::span<float> right{track.right.data(), frames};
        std::fill(left.begin(), left.end(), 0.0F);
        std::fill(right.begin(), right.end(), 0.0F);
        track.instrument->process({left, right}, std::span{block_events.data(), count});
        const StripGain gain{track.gain_left.load(std::memory_order_relaxed),
                             track.gain_right.load(std::memory_order_relaxed)};
        const float peak = mix_into(output, left, right, gain);
        // A block split by the loop point arrives as two chunks; the meter must
        // report the loudest of them, not whichever happened to be last.
        track.peak.store(std::max(track.peak.load(std::memory_order_relaxed), peak),
                         std::memory_order_relaxed);
    }
    const float bus_peak = apply_master(output, master_gain_.load(std::memory_order_relaxed));
    master_peak_.store(std::max(master_peak_.load(std::memory_order_relaxed), bus_peak),
                       std::memory_order_relaxed);
}

void SongEngine::process(StereoBlock output) noexcept {
    if (output.left.size() != output.right.size()) return;
    if (!is_playing() || song_samples_ == 0) {
        std::fill(output.left.begin(), output.left.end(), 0.0F);
        std::fill(output.right.begin(), output.right.end(), 0.0F);
        master_peak_.store(0.0F, std::memory_order_relaxed);
        for (auto& track : tracks_) track->peak.store(0.0F, std::memory_order_relaxed);
        return;
    }
    master_peak_.store(0.0F, std::memory_order_relaxed);
    for (auto& track : tracks_) track->peak.store(0.0F, std::memory_order_relaxed);
    std::size_t rendered = 0;
    while (rendered < output.left.size()) {
        const auto song_position = sample_position_ % song_samples_;
        if (!cursors_valid_ || song_position != continuous_from_) {
            seek_cursors(song_position);
            cursors_valid_ = true;
        }
        const auto until_wrap = song_samples_ - song_position;
        const auto frames = std::min<std::uint64_t>(output.left.size() - rendered, until_wrap);
        process_chunk({output.left.subspan(rendered, frames),
                       output.right.subspan(rendered, frames)}, song_position);
        rendered += static_cast<std::size_t>(frames);
        sample_position_ += frames;
        continuous_from_ = song_position + frames;
    }
}

} // namespace blokkily
