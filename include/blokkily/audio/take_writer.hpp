#pragma once

// Recording audio input into audio clips (item 3.2, feature 4c).
//
// The render callback captures each armed track's raw input into a SampleRing
// (see SongEngine::connect_capture). A TakeWriter empties that ring on a thread
// of its own and writes every track's take to a WAV file as it arrives, so a
// recording costs the callback a copy and nothing else. A take is one
// contiguous run of song samples: a loop wrap, a seek, or frames the ring had
// to drop start a new one, so every take is placed exactly where its audio was
// played, however it was interrupted.
//
// Placing a take (plan C21): the input the callback receives at song sample P
// is what the performer played to the song they heard, which left the engine
// output_latency() samples after it was rendered and came back through the
// device's round trip. A take whose first frame was captured at P therefore
// starts at P - (round trip + output_latency() + Song::record_offset_samples).

#include "blokkily/audio/audio_asset.hpp"
#include "blokkily/audio/sample_ring.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/model/timebase.hpp"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace blokkily {

// One finished take: a WAV file of `frames` at `sample_rate`, the song sample
// its first frame was captured at, and the same audio decoded, for the asset
// cache, so a take is heard without reading back its own file.
struct RecordedTake {
    std::uint32_t track = 0;
    std::uint64_t first_sample = 0;
    std::uint64_t frames = 0;
    std::uint16_t channels = 0;
    std::uint32_t sample_rate = 0;
    std::filesystem::path file;
    AudioAssetPtr audio;
    std::string error; // non-empty when the file could not be written
};

class TakeWriter {
public:
    explicit TakeWriter(std::size_t ring_samples = SampleRing::default_samples);
    ~TakeWriter();
    TakeWriter(const TakeWriter&) = delete;
    TakeWriter& operator=(const TakeWriter&) = delete;

    // What the engine captures into. Lives as long as the writer.
    [[nodiscard]] SampleRing& ring() noexcept { return ring_; }

    // Starts writing takes into `directory` (made when the first take needs
    // it) at `sample_rate`. Whatever the ring held from before is discarded.
    // `threaded` empties the ring on the writer's own thread; without it the
    // ring is emptied only by drain() and finish(), which a test uses to make
    // the ring overflow on purpose. Does nothing while a take is being written.
    void begin(const std::filesystem::path& directory, std::uint32_t sample_rate,
               bool threaded = true);
    // Whether begin() has been called without a finish() since.
    [[nodiscard]] bool active() const noexcept { return active_; }
    // Moves what the ring holds into the takes' files. Only while not threaded.
    void drain();
    // Stops the thread, writes what is left, closes every file and returns
    // the takes, oldest first. Empty when nothing was recorded.
    std::vector<RecordedTake> finish();
    // Frames the ring refused since begin().
    [[nodiscard]] std::uint64_t dropped_frames() const noexcept;
    // The directory the takes of this pass are written into.
    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return directory_; }

private:
    struct Segment;
    void write_chunk(const CaptureHeader& header, const std::vector<float>& samples);
    void close_segment(std::size_t track);
    void run();

    SampleRing ring_;
    std::filesystem::path directory_;
    std::uint32_t sample_rate_ = 0;
    bool active_ = false;
    std::uint64_t dropped_at_begin_ = 0;
    std::vector<std::unique_ptr<Segment>> open_;   // by track
    std::vector<RecordedTake> finished_;
    std::vector<float> scratch_;
    std::vector<float> interleaved_;
    std::uint64_t next_number_ = 1;
    std::thread thread_;
    std::atomic<bool> stop_{false};
};

// Where a take sounds: the tick its clip starts on, the frames of the file
// skipped so that the clip's first frame lands on that tick's sample exactly,
// and how many frames the clip plays. Nothing when no frame of the take is
// left to place.
struct TakePlacement {
    Tick start = 0;
    std::uint64_t offset_frames = 0;
    std::uint64_t length_frames = 0;
};
[[nodiscard]] std::optional<TakePlacement> place_take(const TickClock& clock,
                                                      std::uint64_t first_sample,
                                                      std::uint64_t frames,
                                                      std::uint64_t compensation);

// The samples a take is moved earlier by: the device's round trip, the
// engine's output latency and the song's own correction (never below zero).
[[nodiscard]] std::uint64_t take_compensation(std::uint64_t round_trip,
                                              std::uint64_t output_latency,
                                              std::int64_t record_offset) noexcept;

// Adds the take's file and a clip that plays it where it was heard, on its
// track, through `clock`. The clip's id, or nothing when the take cannot be
// placed (no track, no frames left, or a file that was not written).
std::optional<AudioClipId> commit_take(Song& song, const RecordedTake& take,
                                       const TickClock& clock, std::uint64_t compensation);

} // namespace blokkily
