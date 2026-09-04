#pragma once

#include "blokkily/audio/audio_source.hpp"
#include "blokkily/audio/event_timeline.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace blokkily {

// Plays a whole arrangement: every track renders its own timeline through its
// own instrument, and the mixer sums them. Everything the render callback needs
// is allocated by prepare(); process() neither allocates, locks, nor logs.
class SongEngine final : public AudioSource {
public:
    SongEngine();
    ~SongEngine();
    SongEngine(const SongEngine&) = delete;
    SongEngine& operator=(const SongEngine&) = delete;

    // Instruments are supplied by the caller because instantiating a plugin is
    // a format-specific job that belongs behind the adapters. A track with no
    // instrument stays silent instead of failing the whole song.
    void set_instrument(std::size_t track, std::unique_ptr<PluginInstance> instrument);
    [[nodiscard]] std::size_t track_count() const noexcept { return tracks_.size(); }
    [[nodiscard]] bool has_instrument(std::size_t track) const;

    [[nodiscard]] bool prepare(const Song& song, double bpm, double sample_rate,
                               std::uint32_t maximum_block_size, std::uint64_t seed = 0,
                               std::string* error = nullptr);

    void set_playing(bool playing) noexcept { playing_.store(playing, std::memory_order_release); }
    [[nodiscard]] bool is_playing() const noexcept {
        return playing_.load(std::memory_order_acquire);
    }
    void rewind() noexcept { sample_position_ = 0; }
    void seek(std::uint64_t sample) noexcept { sample_position_ = sample; }
    void process(StereoBlock output) noexcept override;

    // Live mixer moves. Safe to call from the control thread while audio runs:
    // the audio thread only ever reads the resulting gains.
    void set_strip(std::size_t track, const MixerStrip& strip, bool any_solo);
    void apply_mix(const Song& song);
    void set_master_gain_db(double decibels);

    [[nodiscard]] std::uint64_t song_samples() const noexcept { return song_samples_; }
    [[nodiscard]] std::uint32_t maximum_block() const noexcept { return maximum_block_; }
    [[nodiscard]] double sample_rate() const noexcept { return sample_rate_; }
    [[nodiscard]] std::uint64_t sample_position() const noexcept { return sample_position_; }
    // Peak of the last processed block, per track and for the master bus. Read
    // by meters on the control thread.
    [[nodiscard]] float track_peak(std::size_t track) const;
    [[nodiscard]] float master_peak() const noexcept {
        return master_peak_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::vector<std::byte> save_track_state(std::size_t track);
    [[nodiscard]] bool load_track_state(std::size_t track, std::span<const std::byte> state);

private:
    struct TrackPlayback {
        std::unique_ptr<PluginInstance> instrument;
        std::vector<TimedPluginEvent> timeline;
        std::vector<float> left;
        std::vector<float> right;
        std::size_t cursor = 0;
        std::atomic<float> gain_left{1.0F};
        std::atomic<float> gain_right{1.0F};
        std::atomic<float> peak{0.0F};
    };

    void process_chunk(StereoBlock output, std::uint64_t song_position) noexcept;
    // Repoints every track's event cursor after a wrap or a seek, so playback
    // costs one step per event instead of a scan of the song per block.
    void seek_cursors(std::uint64_t position) noexcept;

    std::vector<std::unique_ptr<TrackPlayback>> tracks_;
    std::uint64_t song_samples_ = 0;
    std::uint32_t maximum_block_ = 0;
    double sample_rate_ = 0.0;
    std::uint64_t sample_position_ = 0;
    std::uint64_t continuous_from_ = 0;
    bool cursors_valid_ = false;
    std::atomic<float> master_gain_{1.0F};
    std::atomic<float> master_peak_{0.0F};
    std::atomic<bool> playing_{false};
};

} // namespace blokkily
