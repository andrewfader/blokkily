#pragma once

#include "blokkily/audio/audio_source.hpp"
#include "blokkily/audio/event_queue.hpp"
#include "blokkily/audio/event_timeline.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/model/processor_address.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace blokkily {

// A parameter edit a processor made, stamped on the audio thread with where in
// the song it happened (plan F-D). `rolling` is whether the transport was
// playing the arrangement at the time.
struct PluginEditEvent {
    ProcessorAddress where;
    std::uint64_t song_sample = 0;
    bool rolling = false;
    ParameterEdit edit;
};

// A processor handed back by release_processors(), with the address it had.
struct ReleasedProcessor {
    ProcessorAddress where;
    std::unique_ptr<PluginInstance> instance;
};

namespace engine {
struct TrackPlayback;
struct Arrangement;
struct BusPlayback;
struct TestAccess;
} // namespace engine

// Plays a whole arrangement: every track renders its own timeline through its
// own instrument, and the mixer sums them. Everything the render callback needs
// is allocated by prepare(); process() neither allocates, locks, nor logs.
//
// Each chunk runs in a fixed order (plan F-D): events are collected, the track
// buffer is zeroed, the instrument processes in place, its parameter edits go
// to the edit ring, clip regions and input monitoring are added, the insert
// chain and compensation run, the strip gain is taken, sends are mixed, and the
// track is added to the bus. A track without an instrument still runs every
// stage after the instrument's, so what reaches its strip is heard.
class SongEngine final : public AudioSource {
public:
    SongEngine();
    ~SongEngine();
    SongEngine(const SongEngine&) = delete;
    SongEngine& operator=(const SongEngine&) = delete;

    // Processors are supplied by the caller because instantiating a plugin is
    // a format-specific job that belongs behind the adapters. Control thread,
    // while the device is stopped. Only track instruments are addressable
    // until the insert chains exist (item 2.4); an address the engine cannot
    // hold yet is refused and the instance is destroyed here.
    void set_processor(ProcessorAddress where, std::unique_ptr<PluginInstance> instance);
    // The processor at `where`, or nullptr. Control thread.
    [[nodiscard]] PluginInstance* processor(ProcessorAddress where) const;
    // Takes every processor out of the engine, with its address, so a rebuild
    // can adopt the ones it still needs instead of loading them again. Only
    // after the device has stopped: the callback must not be reading them.
    [[nodiscard]] std::vector<ReleasedProcessor> release_processors();
    // Pushes a state into a running processor, for a processor that accepts
    // one while it runs. False, and nothing loaded, for any other.
    [[nodiscard]] bool update_processor_state(ProcessorAddress where,
                                              std::span<const std::byte> state);
    // The next parameter edit a processor reported, oldest first. Control
    // thread. A full ring drops edits rather than holding up the callback.
    bool take_plugin_edit(PluginEditEvent& edit) noexcept { return edits_.pop(edit); }

    // A track's instrument; the same as set_processor(track_instrument(track)).
    void set_instrument(std::size_t track, std::unique_ptr<PluginInstance> instrument);
    [[nodiscard]] std::size_t track_count() const noexcept { return tracks_.size(); }
    [[nodiscard]] bool has_instrument(std::size_t track) const;

    // Builds everything the render callback needs for `song` at `sample_rate`.
    // The song's tempo map says where each tick falls (plan F-A); there is no
    // other tempo.
    [[nodiscard]] bool prepare(const Song& song, double sample_rate,
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
    //
    // A new tempo or meter is a recompile too. The playhead keeps its musical
    // position across it: the block that picks the new arrangement up moves
    // the song position to the sample where the same tick falls under the new
    // clock, unless a seek was taken in that same block (the seek was already
    // placed with the new clock).
    [[nodiscard]] bool recompile(const Song& song, std::uint64_t seed = 0,
                                 std::string* error = nullptr);
    // The clock of the arrangement last prepared or recompiled: what the
    // control thread converts ticks and samples with. Control thread only.
    [[nodiscard]] const TickClock& published_clock() const noexcept { return published_clock_; }

    // A note played from the interface rather than from the arrangement. Safe
    // to call while audio runs: the control thread only ever writes, the audio
    // thread only ever reads, and neither allocates or blocks. Sounds whether
    // or not the transport is playing, so a keyboard works on a stopped song.
    bool play_live(std::size_t track, const PluginEvent& event) noexcept;

    // Plays whatever arrives on an input — a MIDI port — on the track each
    // event names, at the start of the next block, whether or not the
    // transport is running. The queue belongs to the caller and must outlive
    // the connection; nullptr disconnects. One engine reads a queue at a time.
    void connect_input(InputQueue* input) noexcept {
        input_.store(input, std::memory_order_release);
    }
    [[nodiscard]] InputQueue* input() const noexcept {
        return input_.load(std::memory_order_acquire);
    }
    // A note played on an on-screen surface - the keyboard panel, the tracker's
    // note keys - for `track`. It is input like a MIDI port's: it sounds at the
    // start of the next block, and while recording with the transport running
    // it is captured into the take the same way. Control thread only (the
    // queue's one producer); never allocates or blocks. False when the track
    // does not exist or the queue is full.
    bool perform(std::size_t track, const PluginEvent& event) noexcept;
    // While recording, every input event played with the transport running is
    // captured with the song position it sounded at, for the control thread
    // to write into the song. What is recorded is what was heard.
    void set_recording(bool recording) noexcept {
        recording_.store(recording, std::memory_order_release);
    }
    [[nodiscard]] bool is_recording() const noexcept {
        return recording_.load(std::memory_order_acquire);
    }
    // The next captured event, oldest first. Control thread only.
    bool take_captured(CapturedEvent& event) noexcept { return captured_.pop(event); }

    void set_playing(bool playing) noexcept {
        if (!playing) stop_requested_.store(true, std::memory_order_release);
        playing_.store(playing, std::memory_order_release);
    }
    [[nodiscard]] bool is_playing() const noexcept {
        return playing_.load(std::memory_order_acquire);
    }
    void rewind() noexcept { seek(0); }
    void seek(std::uint64_t sample) noexcept {
        requested_position_.store(sample, std::memory_order_release);
    }
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
    [[nodiscard]] std::uint64_t sample_position() const noexcept {
        const auto requested = requested_position_.load(std::memory_order_acquire);
        return requested == no_seek ? published_position_.load(std::memory_order_acquire)
                                    : requested;
    }
    // Peak of the last processed block, per track and for the master bus. Read
    // by meters on the control thread.
    [[nodiscard]] float track_peak(std::size_t track) const;
    [[nodiscard]] float master_peak() const noexcept {
        return master_peak_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::vector<std::byte> save_track_state(std::size_t track);
    [[nodiscard]] bool load_track_state(std::size_t track, std::span<const std::byte> state);

private:
    friend struct engine::TestAccess;
    using Arrangement = engine::Arrangement;
    using TrackPlayback = engine::TrackPlayback;
    // One compiled arrangement per slot (src/audio/engine/arrangement.hpp).
    static constexpr std::size_t arrangement_slots = 3;

    // `from_timeline` is false when the transport is stopped: the arrangement
    // contributes nothing, but live notes and ringing tails still do.
    void process_chunk(StereoBlock output, std::uint64_t song_position,
                       bool from_timeline) noexcept;
    // Chunk stage 1: gathers what one track plays this chunk into its scratch.
    // `capture_tick` is the song tick of `song_position` under the clock being
    // played, stamped on whatever input is captured.
    std::size_t collect_events(TrackPlayback& track, std::size_t index,
                               std::uint64_t song_position, std::uint64_t end,
                               bool from_timeline, bool capture, Tick capture_tick) noexcept;
    // Chunk stage 4: moves what a processor reported into the edit ring.
    void drain_edits(PluginInstance& processor, ProcessorAddress where,
                     std::uint64_t song_position, bool from_timeline) noexcept;
    // The owner of a processor address, or nullptr when the engine has none.
    [[nodiscard]] std::unique_ptr<PluginInstance>* processor_slot(ProcessorAddress where) const;
    // Repoints every track's event cursor after a wrap or a seek, so playback
    // costs one step per event instead of a scan of the song per block.
    void seek_cursors(std::uint64_t position) noexcept;
    // Installs a queued arrangement, if one is waiting, and keeps the playhead
    // on its tick under the new clock unless `seeked` (a seek was taken in
    // this block). Called by the render callback and by nothing else.
    void take_queued_arrangement(bool seeked) noexcept;
    // The timeline the render callback is playing for one track.
    [[nodiscard]] const std::vector<TimedPluginEvent>& timeline_for(
        std::size_t track) const noexcept;
    // Compiles `song` into `target`. Control thread only.
    [[nodiscard]] bool compile_into(Arrangement& target, const Song& song,
                                    std::uint64_t seed, std::string* error) const;

    std::vector<std::unique_ptr<TrackPlayback>> tracks_;
    std::unique_ptr<engine::BusPlayback> buses_;
    std::array<std::unique_ptr<Arrangement>, arrangement_slots> arrangements_;
    // Handed from the control thread to the render callback, and back again as
    // the callback reports what it is reading. A slot named by neither is free
    // for the next recompile to fill.
    std::atomic<Arrangement*> queued_{nullptr};
    std::atomic<Arrangement*> rendering_{nullptr};
    std::atomic<std::uint64_t> published_song_samples_{0};
    // The clock of the last arrangement handed to the callback. Control thread.
    TickClock published_clock_;
    // What the render callback is playing. Touched by the callback alone.
    Arrangement* live_ = nullptr;
    std::uint64_t song_samples_ = 0;
    std::uint32_t maximum_block_ = 0;
    double sample_rate_ = 0.0;
    std::uint64_t sample_position_ = 0;
    static constexpr auto no_seek = std::numeric_limits<std::uint64_t>::max();
    std::atomic<std::uint64_t> requested_position_{no_seek};
    std::atomic<std::uint64_t> published_position_{0};
    std::atomic<bool> stop_requested_{false};
    std::uint64_t continuous_from_ = 0;
    bool cursors_valid_ = false;
    // Set when a new arrangement is installed: what the old one left sounding
    // is let go before the new one is played.
    bool release_arrangement_notes_ = false;
    std::atomic<float> master_gain_{1.0F};
    std::atomic<float> master_peak_{0.0F};
    std::atomic<bool> playing_{false};
    std::atomic<InputQueue*> input_{nullptr};
    std::atomic<bool> recording_{false};
    // Every armed track captures its own copy of an input event, so the ring
    // holds a burst of chords into sixty-four armed tracks between drains.
    SpscQueue<CapturedEvent, 8192> captured_;
    // What the on-screen surfaces performed, waiting for the next block.
    PerformQueue performed_;
    // Parameter edits the processors reported, stamped with the song sample.
    SpscQueue<PluginEditEvent, 1024> edits_;
    // What one processor reported in one call, before it is stamped. Touched
    // by the callback alone.
    std::array<ParameterEdit, 64> edit_scratch_{};
    // What the input and the surfaces delivered for the block being rendered,
    // before it is handed to the tracks it names. Touched by the callback alone.
    std::array<RoutedEvent, InputQueue::capacity() + PerformQueue::capacity()> incoming_{};
    std::size_t incoming_count_ = 0;
};

} // namespace blokkily
