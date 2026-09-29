#pragma once

// Disk streaming (wave 4.2): an audio clip whose file is too large to hold in
// memory plays from a ring buffer that a background thread keeps filled from
// the file.
//
// Three roles, each with its own thread discipline:
//
//  * StreamReader decodes a file and resamples it to the engine rate with the
//    same libsamplerate converter the in-memory path uses (resample() in
//    audio_file.hpp), so a streamed clip plays the samples an in-memory clip
//    would. It seeks to any frame at the engine rate. Producer side only: it
//    performs file I/O and is never touched by the render callback.
//
//  * The consumer (the render callback) owns the read side of the ring and
//    every decision about position. It asks for a position with one atomic
//    word (a generation and a frame); it never resets the ring, and it never
//    waits, locks, allocates or reads the file.
//
//  * The producer (a DiskStreamService worker, or the thread of an offline
//    bounce) owns the write side and the reader. When it sees a new
//    generation it seeks, publishes where in the ring that generation's data
//    begins, and writes from there. The consumer skips its read position to
//    that point itself. Producers serialise on a mutex the consumer never
//    takes.
//
// A stream that runs dry fades out over `fade_frames` samples of audio it
// still holds (it always keeps that many in reserve while it plays), stays
// silent while it waits, and fades back in when the data is there again, so
// a slow disk is heard as a short dip rather than a click. A locate or a loop
// wrap that lands where the ring has no data is handled the same way: the
// audio playing fades out, the new position is requested, and it fades in
// when it arrives. An offline render (a bounce) reads blocking instead: the
// thread that renders fills the ring itself before it reads, so an export
// never starves and is the engine's render sample for sample.

#include "blokkily/audio/audio_file.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace blokkily {

// Decodes a file at the engine rate, from any frame. Not thread safe; not for
// the audio thread.
class StreamReader {
public:
    StreamReader();
    ~StreamReader();
    StreamReader(const StreamReader&) = delete;
    StreamReader& operator=(const StreamReader&) = delete;

    bool open(const std::filesystem::path& path, double engine_rate, std::string* error = nullptr);
    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] const AudioFileInfo& native() const noexcept { return native_; }
    // Frames of the file at the engine rate: round(native frames x ratio),
    // exactly as many as resample() returns.
    [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }
    // The next read() starts at `frame` (engine rate).
    void seek(std::uint64_t frame);
    // Fills `count` frames of both channels from the current position and
    // advances it. Past the end of the file the frames are silence. A mono
    // file reads the same samples into both channels.
    void read(float* left, float* right, std::size_t count);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    AudioFileInfo native_{};
    std::uint64_t frames_ = 0;
};

// Single-producer single-consumer ring of stereo frames. Positions count every
// frame ever written or read, so they never wrap in practice.
class AudioStreamRingBuffer {
public:
    explicit AudioStreamRingBuffer(std::size_t capacity_frames = 65536);
    AudioStreamRingBuffer(const AudioStreamRingBuffer&) = delete;
    AudioStreamRingBuffer& operator=(const AudioStreamRingBuffer&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    // Either side.
    [[nodiscard]] std::size_t available_read() const noexcept;
    [[nodiscard]] std::size_t available_write() const noexcept;
    // Producer only.
    std::size_t write(std::span<const float> left, std::span<const float> right) noexcept;
    [[nodiscard]] std::uint64_t write_position() const noexcept {
        return write_pos_.load(std::memory_order_acquire);
    }
    [[nodiscard]] std::uint64_t read_position() const noexcept {
        return read_pos_.load(std::memory_order_acquire);
    }
    // Consumer only: copies up to `left.size()` frames out and zero-fills the
    // rest. Returns the frames read.
    std::size_t read(std::span<float> left, std::span<float> right) noexcept;
    // Consumer only: the frame `offset` frames past the read position.
    [[nodiscard]] float left_at(std::size_t offset) const noexcept;
    [[nodiscard]] float right_at(std::size_t offset) const noexcept;
    // Consumer only: discards `frames` readable frames.
    void skip(std::size_t frames) noexcept;
    // Consumer only: moves the read position forward to `position`, which
    // the producer has already written up to.
    void skip_to(std::uint64_t position) noexcept;

private:
    std::size_t capacity_;
    std::vector<float> left_;
    std::vector<float> right_;
    alignas(64) std::atomic<std::uint64_t> write_pos_{0};
    alignas(64) std::atomic<std::uint64_t> read_pos_{0};
};

// One clip's stream: its reader, its ring, and the position handshake.
class DiskStream {
public:
    // Samples of fade on starvation, recovery and relocation; also the
    // reserve kept in the ring while playing, so a fade-out always has audio.
    static constexpr std::size_t fade_frames = 128;

    // The stream starts positioned at `first_frame`, where its clip begins,
    // so the first fill already holds what the clip plays first.
    DiskStream(const std::filesystem::path& path, double engine_rate,
               std::uint64_t first_frame = 0, std::size_t ring_capacity = 65536);
    ~DiskStream();
    DiskStream(const DiskStream&) = delete;
    DiskStream& operator=(const DiskStream&) = delete;

    [[nodiscard]] bool is_open() const noexcept { return open_; }
    [[nodiscard]] std::uint64_t frames() const noexcept { return frames_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    // --- Consumer (the render callback, or an offline bounce's thread) ------
    // Writes the stream's frames [frame, frame + count) into `left` and
    // `right` (overwriting them), where count is their length. Live
    // (`blocking` false): never waits; a frame the ring does not hold yet is
    // silence, with the fades described above. Blocking: fills the ring on
    // this thread first (taking the producer lock), so every frame is there;
    // only an offline render with the device stopped may block.
    void read(std::uint64_t frame, std::span<float> left, std::span<float> right,
              bool blocking) noexcept;
    // Asks for `frame` to be where the next read() starts, ahead of time: a
    // clip about to begin, or the playhead parked in one. Never waits.
    void cue(std::uint64_t frame) noexcept;
    // Whether a live read() has had to play silence or fade for want of data
    // since the flag was last cleared. Any thread.
    [[nodiscard]] bool has_underrun() const noexcept {
        return underrun_.load(std::memory_order_relaxed);
    }
    void clear_underrun() noexcept { underrun_.store(false, std::memory_order_relaxed); }

    // --- Producer (a service worker, or a blocking consumer) ----------------
    // Serves a pending position request and writes whatever the ring has
    // room for. Returns true if it wrote anything or served a request.
    bool fill();
    // Frames readable now (either side).
    [[nodiscard]] std::size_t buffered() const noexcept { return ring_.available_read(); }

private:
    static constexpr int frame_bits = 40;
    static constexpr std::uint64_t frame_mask = (std::uint64_t{1} << frame_bits) - 1;
    static std::uint64_t pack(std::uint32_t generation, std::uint64_t frame) noexcept {
        return (static_cast<std::uint64_t>(generation) << frame_bits) | (frame & frame_mask);
    }
    void request(std::uint64_t frame) noexcept;
    // Takes up a served request: the ring's read side jumps to it.
    void poll() noexcept;
    // Copies `count` ring frames to out[at..], with the gain ramping by
    // `step` per sample (clamped to 0..1). Stops early when the gain reaches
    // zero on a fade-out; returns frames consumed.
    std::size_t play(std::span<float> left, std::span<float> right, std::size_t at,
                     std::size_t count, float step) noexcept;

    std::filesystem::path path_;
    bool open_ = false;
    std::uint64_t frames_ = 0;
    AudioStreamRingBuffer ring_;

    // The handshake.
    std::atomic<std::uint64_t> request_{0};
    std::atomic<std::uint64_t> served_{0};
    std::atomic<std::uint64_t> generation_start_{0};
    std::atomic<bool> underrun_{false};

    // Consumer-owned.
    std::uint32_t generation_ = 0;
    std::uint64_t requested_frame_ = 0;
    bool synced_ = true;
    std::uint64_t read_frame_ = 0;
    std::uint64_t want_ = 0;
    float gain_ = 1.0F;

    // Producer-owned, under producer_mutex_.
    std::mutex producer_mutex_;
    StreamReader reader_;
    std::uint32_t producer_generation_ = 0;
    std::vector<float> scratch_left_;
    std::vector<float> scratch_right_;
};

// Keeps registered streams' rings full on background threads. With zero
// threads nothing fills them until service() is called, which a test uses to
// decide exactly when the disk "answers".
class DiskStreamService {
public:
    explicit DiskStreamService(std::size_t thread_count = 1);
    ~DiskStreamService();
    DiskStreamService(const DiskStreamService&) = delete;
    DiskStreamService& operator=(const DiskStreamService&) = delete;

    // Control thread. The service holds streams weakly: dropping the last
    // owner ends the stream.
    void register_stream(const std::shared_ptr<DiskStream>& stream);
    // One pass over every stream on the calling thread (never the audio
    // thread). Returns true if any stream took data.
    bool service();
    // Services until no stream takes more (every ring full or its request
    // served), on the calling thread.
    void service_until_idle();
    [[nodiscard]] std::size_t thread_count() const noexcept { return workers_.size(); }

private:
    void worker_loop();
    std::vector<std::shared_ptr<DiskStream>> snapshot();

    std::vector<std::thread> workers_;
    std::vector<std::weak_ptr<DiskStream>> streams_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
};

} // namespace blokkily
