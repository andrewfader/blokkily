#pragma once

#include "blokkily/audio/audio_source.hpp"
#include "blokkily/audio/event_timeline.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <array>
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

    // Recompiles what the arrangement plays and hands it to the render callback
    // without rebuilding the graph, reloading an instrument, or moving the
    // playhead. Editing a step is a change to the music, not to the machine
    // playing it, so the song keeps running through the edit.
    //
    // Safe to call from the control thread while audio runs: the callback only
    // ever reads, the swap is a single pointer store, and neither side
    // allocates or blocks to make it. Refuses a song whose track list no longer
    // matches the prepared graph, because that needs instruments the engine
    // does not hold; the caller rebuilds for those.
    [[nodiscard]] bool recompile(const Song& song, double bpm, std::uint64_t seed = 0,
                                 std::string* error = nullptr);

    // A note played from the interface rather than from the arrangement. Safe
    // to call while audio runs: the control thread only ever writes, the audio
    // thread only ever reads, and neither allocates or blocks. Sounds whether
    // or not the transport is playing, so a keyboard works on a stopped song.
    bool play_live(std::size_t track, const PluginEvent& event) noexcept;

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

    // The length of the arrangement the engine will play next. This is the
    // published value rather than the one the callback is midway through, so a
    // bounce started right after an edit measures the song it is about to
    // render.
    [[nodiscard]] std::uint64_t song_samples() const noexcept {
        return published_song_samples_.load(std::memory_order_acquire);
    }
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
    // A single-writer, single-reader ring of events waiting for the next block.
    struct LiveEvents {
        static constexpr std::size_t capacity = 128;
        std::array<PluginEvent, capacity> events{};
        std::atomic<std::size_t> written{0};
        std::atomic<std::size_t> read{0};
    };

    // One compiled arrangement: a sample timeline per track and the length they
    // were compiled against. Three of these are owned for the life of the
    // engine, so a recompile always has a slot to fill that is neither being
    // rendered nor already queued, and publishing one costs a pointer store
    // instead of an allocation the render callback would have to wait for.
    struct Arrangement {
        std::vector<std::vector<TimedPluginEvent>> timelines;
        std::uint64_t song_samples = 0;
    };
    static constexpr std::size_t arrangement_slots = 3;

    struct TrackPlayback {
        std::unique_ptr<PluginInstance> instrument;
        std::vector<float> left;
        std::vector<float> right;
        std::size_t cursor = 0;
        // How many note-ons the arrangement has sent for each key without a
        // note-off. Read and written by the render callback alone. A note whose
        // step is erased mid-flight has no note-off left in the timeline, so
        // without this it would ring for ever.
        std::array<std::uint8_t, 128> sounding{};
        std::atomic<float> gain_left{1.0F};
        std::atomic<float> gain_right{1.0F};
        std::atomic<float> peak{0.0F};
        LiveEvents live;
    };

    // `from_timeline` is false when the transport is stopped: the arrangement
    // contributes nothing, but live notes and ringing tails still do.
    void process_chunk(StereoBlock output, std::uint64_t song_position,
                       bool from_timeline) noexcept;
    // Repoints every track's event cursor after a wrap or a seek, so playback
    // costs one step per event instead of a scan of the song per block.
    void seek_cursors(std::uint64_t position) noexcept;
    // Installs a queued arrangement, if one is waiting. Called by the render
    // callback and by nothing else.
    void take_queued_arrangement() noexcept;
    // The timeline the render callback is playing for one track.
    [[nodiscard]] const std::vector<TimedPluginEvent>& timeline_for(
        std::size_t track) const noexcept;
    // Compiles `song` into `target`. Control thread only.
    [[nodiscard]] bool compile_into(Arrangement& target, const Song& song, double bpm,
                                    std::uint64_t seed, std::string* error) const;

    std::vector<std::unique_ptr<TrackPlayback>> tracks_;
    std::array<std::unique_ptr<Arrangement>, arrangement_slots> arrangements_;
    // Handed from the control thread to the render callback, and back again as
    // the callback reports what it is reading. A slot named by neither is free
    // for the next recompile to fill.
    std::atomic<Arrangement*> queued_{nullptr};
    std::atomic<Arrangement*> rendering_{nullptr};
    std::atomic<std::uint64_t> published_song_samples_{0};
    // What the render callback is playing. Touched by the callback alone.
    Arrangement* live_ = nullptr;
    std::uint64_t song_samples_ = 0;
    std::uint32_t maximum_block_ = 0;
    double sample_rate_ = 0.0;
    std::uint64_t sample_position_ = 0;
    std::uint64_t continuous_from_ = 0;
    bool cursors_valid_ = false;
    // Set when a new arrangement is installed: what the old one left sounding
    // is let go before the new one is played.
    bool release_arrangement_notes_ = false;
    std::atomic<float> master_gain_{1.0F};
    std::atomic<float> master_peak_{0.0F};
    std::atomic<bool> playing_{false};
};

} // namespace blokkily
