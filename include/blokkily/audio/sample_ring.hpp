#pragma once

// The hand-off from the render callback to the thread that writes recorded
// audio to disk (item 3.2). The callback pushes each chunk of raw input it
// captured for a track, stamped with the song sample it was rendered at; the
// take writer pops them in order. One writer, one reader; neither allocates,
// locks or waits. A full ring refuses the chunk and counts its frames as
// dropped, because a gap in a take is better than a stalled callback, and the
// stamp on the next chunk that fits tells the reader exactly where the gap was.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace blokkily {

// What one captured chunk was. Samples follow channel by channel: every frame
// of the first channel, then every frame of the second.
struct CaptureHeader {
    std::uint64_t song_sample = 0; // where the song was as the chunk was rendered
    std::uint32_t track = 0;
    std::uint32_t frames = 0;
    std::uint16_t channels = 0;    // 1 or 2
};

class SampleRing {
public:
    // About twenty seconds of stereo at 48 kHz before a stalled reader drops
    // anything.
    static constexpr std::size_t default_samples = std::size_t{1} << 21;
    static constexpr std::size_t header_capacity = 4096;

    explicit SampleRing(std::size_t sample_capacity = default_samples)
        : samples_(sample_capacity == 0 ? 1 : sample_capacity), headers_(header_capacity) {}
    SampleRing(const SampleRing&) = delete;
    SampleRing& operator=(const SampleRing&) = delete;

    // Audio thread. `second` is empty for a mono chunk, else as long as
    // `first`. False, with the frames counted as dropped, when the ring has no
    // room for the whole chunk.
    bool push(std::uint32_t track, std::uint64_t song_sample, std::span<const float> first,
              std::span<const float> second) noexcept {
        const auto frames = first.size();
        if (frames == 0) return true;
        const std::uint16_t channels = second.empty() ? 1 : 2;
        if (channels == 2 && second.size() != frames) return false;
        const auto needed = frames * channels;
        const auto read = read_samples_.load(std::memory_order_acquire);
        const auto header_read = read_headers_.load(std::memory_order_acquire);
        if (needed > samples_.size() - (written_samples_ - read) ||
            written_headers_ - header_read >= headers_.size()) {
            dropped_.fetch_add(frames, std::memory_order_relaxed);
            return false;
        }
        copy_in(first, written_samples_);
        if (channels == 2) copy_in(second, written_samples_ + frames);
        written_samples_ += needed;
        headers_[written_headers_ % headers_.size()] = {
            song_sample, track, static_cast<std::uint32_t>(frames), channels};
        published_headers_.store(++written_headers_, std::memory_order_release);
        return true;
    }

    // Reader thread. The next chunk, oldest first, with its samples in
    // `samples` (resized to frames x channels). False when nothing waits.
    bool pop(CaptureHeader& header, std::vector<float>& samples) {
        const auto next = read_headers_.load(std::memory_order_relaxed);
        if (next == published_headers_.load(std::memory_order_acquire)) return false;
        header = headers_[next % headers_.size()];
        const auto count = static_cast<std::size_t>(header.frames) * header.channels;
        samples.resize(count);
        const auto from = read_samples_.load(std::memory_order_relaxed);
        for (std::size_t index = 0; index < count; ++index)
            samples[index] = samples_[(from + index) % samples_.size()];
        read_samples_.store(from + count, std::memory_order_release);
        read_headers_.store(next + 1, std::memory_order_release);
        return true;
    }

    // Frames refused since construction, any thread.
    [[nodiscard]] std::uint64_t dropped_frames() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::size_t sample_capacity() const noexcept { return samples_.size(); }

private:
    void copy_in(std::span<const float> from, std::size_t at) noexcept {
        for (std::size_t index = 0; index < from.size(); ++index)
            samples_[(at + index) % samples_.size()] = from[index];
    }

    std::vector<float> samples_;
    std::vector<CaptureHeader> headers_;
    // Writer side: owned by the audio thread.
    std::size_t written_samples_ = 0;
    std::size_t written_headers_ = 0;
    std::atomic<std::size_t> published_headers_{0};
    // Reader side.
    std::atomic<std::size_t> read_samples_{0};
    std::atomic<std::size_t> read_headers_{0};
    std::atomic<std::uint64_t> dropped_{0};
};

} // namespace blokkily
