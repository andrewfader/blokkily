#pragma once

#include "blokkily/audio/audio_file.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace blokkily {

// Lock-free single-producer single-consumer ring buffer for stereo audio frames.
// Producer: background disk reader thread.
// Consumer: real-time audio thread.
class AudioStreamRingBuffer {
public:
    explicit AudioStreamRingBuffer(std::size_t capacity_frames = 65536);
    ~AudioStreamRingBuffer() = default;

    AudioStreamRingBuffer(const AudioStreamRingBuffer&) = delete;
    AudioStreamRingBuffer& operator=(const AudioStreamRingBuffer&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }
    [[nodiscard]] std::size_t available_read() const noexcept;
    [[nodiscard]] std::size_t available_write() const noexcept;

    // Called only from worker/producer thread. Returns number of frames written.
    std::size_t write(std::span<const float> left, std::span<const float> right) noexcept;

    // Called only from audio/consumer thread (zero locks, zero allocations).
    // Returns number of frames read.
    std::size_t read(std::span<float> left, std::span<float> right) noexcept;

    // Resets read and write pointers to empty.
    void reset() noexcept;

    [[nodiscard]] std::uint64_t current_read_pos() const noexcept {
        return read_pos_.load(std::memory_order_relaxed);
    }
    void advance_read(std::size_t frames) noexcept {
        read_pos_.store(read_pos_.load(std::memory_order_relaxed) + frames, std::memory_order_release);
    }
    [[nodiscard]] const std::vector<float>& left_channel() const noexcept { return left_; }
    [[nodiscard]] const std::vector<float>& right_channel() const noexcept { return right_; }

private:
    std::size_t capacity_;
    std::vector<float> left_;
    std::vector<float> right_;
    alignas(64) std::atomic<std::uint64_t> write_pos_{0};
    alignas(64) std::atomic<std::uint64_t> read_pos_{0};
};

// Represents a streaming audio source reading a file from disk into a ring buffer.
class DiskStream {
public:
    explicit DiskStream(std::filesystem::path path, std::size_t ring_capacity = 65536);
    ~DiskStream();

    DiskStream(const DiskStream&) = delete;
    DiskStream& operator=(const DiskStream&) = delete;

    [[nodiscard]] bool is_open() const noexcept;
    [[nodiscard]] const AudioFileInfo& info() const noexcept { return info_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    // Audio thread: reads frames from ring buffer and sums into output with soft fade on underrun.
    // Completely wait-free, zero allocation, zero lock.
    void read_and_sum(StereoBlock buffer, float gain = 1.0F) noexcept;

    // Underrun status flag
    [[nodiscard]] bool has_underrun() const noexcept { return underrun_.load(std::memory_order_relaxed); }
    void clear_underrun() noexcept { underrun_.store(false, std::memory_order_relaxed); }

    // Control thread: request seek to sample offset.
    void seek(std::uint64_t target_frame);

    // Worker thread: perform background disk read into ring buffer.
    // Returns true if frames were written or buffer is full, false if EOF or error.
    bool fill_buffer(std::size_t chunk_frames = 8192);

    [[nodiscard]] AudioStreamRingBuffer& ring_buffer() noexcept { return ring_; }
    [[nodiscard]] std::uint64_t file_position() const noexcept { return current_file_frame_.load(std::memory_order_relaxed); }

private:
    std::filesystem::path path_;
    AudioFileInfo info_{};
    void* sndfile_handle_ = nullptr; // SNDFILE*
    AudioStreamRingBuffer ring_;
    std::atomic<bool> underrun_{false};
    std::atomic<std::uint64_t> current_file_frame_{0};
    std::atomic<std::int64_t> seek_request_{-1};
    float fade_gain_{1.0F};
    std::vector<float> scratch_interleaved_;
    std::vector<float> scratch_left_;
    std::vector<float> scratch_right_;
    std::mutex file_mutex_;
};

// Background worker service managing pre-buffering across active disk streams.
class DiskStreamService {
public:
    explicit DiskStreamService(std::size_t thread_count = 1);
    ~DiskStreamService();

    DiskStreamService(const DiskStreamService&) = delete;
    DiskStreamService& operator=(const DiskStreamService&) = delete;

    std::shared_ptr<DiskStream> open_stream(const std::filesystem::path& path,
                                            std::size_t ring_capacity = 65536);
    void register_stream(std::shared_ptr<DiskStream> stream);
    void unregister_stream(const std::shared_ptr<DiskStream>& stream);

    // Notify workers that streams need filling or seek was requested.
    void notify();

    // Block until all registered streams have filled their buffers up to at least target_frames.
    void prefill(std::size_t target_frames = 16384);

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::vector<std::shared_ptr<DiskStream>> streams_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::atomic<bool> stop_{false};
};

} // namespace blokkily
