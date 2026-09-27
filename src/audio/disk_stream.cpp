#include "blokkily/audio/disk_stream.hpp"

#include <sndfile.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace blokkily {

AudioStreamRingBuffer::AudioStreamRingBuffer(std::size_t capacity_frames)
    : capacity_(std::max<std::size_t>(1024, capacity_frames)),
      left_(capacity_, 0.0F),
      right_(capacity_, 0.0F) {}

std::size_t AudioStreamRingBuffer::available_read() const noexcept {
    const std::uint64_t w = write_pos_.load(std::memory_order_acquire);
    const std::uint64_t r = read_pos_.load(std::memory_order_relaxed);
    return w >= r ? static_cast<std::size_t>(w - r) : 0;
}

std::size_t AudioStreamRingBuffer::available_write() const noexcept {
    const std::uint64_t w = write_pos_.load(std::memory_order_relaxed);
    const std::uint64_t r = read_pos_.load(std::memory_order_acquire);
    const std::uint64_t used = w >= r ? (w - r) : 0;
    return used < capacity_ ? static_cast<std::size_t>(capacity_ - used) : 0;
}

std::size_t AudioStreamRingBuffer::write(std::span<const float> left,
                                        std::span<const float> right) noexcept {
    const std::size_t avail = available_write();
    const std::size_t count = std::min({left.size(), right.size(), avail});
    if (count == 0) return 0;

    const std::uint64_t w = write_pos_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t idx = static_cast<std::size_t>((w + i) % capacity_);
        left_[idx] = left[i];
        right_[idx] = right[i];
    }
    write_pos_.store(w + count, std::memory_order_release);
    return count;
}

std::size_t AudioStreamRingBuffer::read(std::span<float> left,
                                       std::span<float> right) noexcept {
    const std::size_t requested = std::min(left.size(), right.size());
    const std::size_t avail = available_read();
    const std::size_t count = std::min(requested, avail);
    const std::uint64_t r = read_pos_.load(std::memory_order_relaxed);

    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t idx = static_cast<std::size_t>((r + i) % capacity_);
        left[i] = left_[idx];
        right[i] = right_[idx];
    }
    if (count < requested) {
        std::fill(left.begin() + count, left.begin() + requested, 0.0F);
        std::fill(right.begin() + count, right.begin() + requested, 0.0F);
    }
    read_pos_.store(r + count, std::memory_order_release);
    return count;
}

void AudioStreamRingBuffer::reset() noexcept {
    write_pos_.store(0, std::memory_order_release);
    read_pos_.store(0, std::memory_order_release);
}

DiskStream::DiskStream(std::filesystem::path path, std::size_t ring_capacity)
    : path_(std::move(path)), ring_(ring_capacity) {
    SF_INFO sf_info{};
    std::memset(&sf_info, 0, sizeof(sf_info));
    SNDFILE* handle = sf_open(path_.string().c_str(), SFM_READ, &sf_info);
    if (handle != nullptr) {
        sndfile_handle_ = handle;
        info_.frames = static_cast<std::uint64_t>(std::max<sf_count_t>(0, sf_info.frames));
        info_.sample_rate = static_cast<std::uint32_t>(std::max(0, sf_info.samplerate));
        info_.channels = static_cast<std::uint16_t>(std::max(0, sf_info.channels));

        const std::size_t default_chunk = 8192;
        scratch_interleaved_.assign(default_chunk * std::max<std::uint16_t>(1, info_.channels), 0.0F);
        scratch_left_.assign(default_chunk, 0.0F);
        scratch_right_.assign(default_chunk, 0.0F);
    }
}

DiskStream::~DiskStream() {
    std::lock_guard lock(file_mutex_);
    if (sndfile_handle_ != nullptr) {
        sf_close(reinterpret_cast<SNDFILE*>(sndfile_handle_));
        sndfile_handle_ = nullptr;
    }
}

bool DiskStream::is_open() const noexcept {
    return sndfile_handle_ != nullptr;
}

void DiskStream::read_and_sum(StereoBlock buffer, float gain) noexcept {
    const std::size_t requested = std::min(buffer.left.size(), buffer.right.size());
    if (requested == 0) return;

    const std::size_t avail = ring_.available_read();
    const std::size_t count = std::min(requested, avail);

    if (count < requested) {
        underrun_.store(true, std::memory_order_relaxed);
    }

    // Soft fade transition to avoid pops on buffer starvation or resumption
    const float fade_target = (count == requested) ? 1.0F : 0.0F;
    const float fade_delta = (fade_target - fade_gain_) / static_cast<float>(requested);

    if (count > 0) {
        const std::uint64_t r = ring_.current_read_pos();
        const std::size_t cap = ring_.capacity();
        const auto& ring_left = ring_.left_channel();
        const auto& ring_right = ring_.right_channel();

        for (std::size_t i = 0; i < count; ++i) {
            fade_gain_ += fade_delta;
            const std::size_t idx = static_cast<std::size_t>((r + i) % cap);
            const float scale = gain * fade_gain_;
            buffer.left[i] += ring_left[idx] * scale;
            buffer.right[i] += ring_right[idx] * scale;
        }
        ring_.advance_read(count);
    } else {
        fade_gain_ = 0.0F;
    }
    if (count == requested && std::abs(fade_gain_ - 1.0F) < 1e-4F) {
        fade_gain_ = 1.0F;
    }
}

void DiskStream::seek(std::uint64_t target_frame) {
    seek_request_.store(static_cast<std::int64_t>(target_frame), std::memory_order_release);
}

bool DiskStream::fill_buffer(std::size_t chunk_frames) {
    std::lock_guard lock(file_mutex_);
    if (sndfile_handle_ == nullptr) return false;
    auto* handle = reinterpret_cast<SNDFILE*>(sndfile_handle_);

    const std::int64_t requested_seek = seek_request_.exchange(-1, std::memory_order_acq_rel);
    if (requested_seek >= 0) {
        sf_seek(handle, static_cast<sf_count_t>(requested_seek), SEEK_SET);
        current_file_frame_.store(static_cast<std::uint64_t>(requested_seek), std::memory_order_release);
        ring_.reset();
        fade_gain_ = 1.0F;
    }

    const std::size_t avail = ring_.available_write();
    if (avail < 512) {
        return true; // Buffer has plenty of headroom
    }

    const std::size_t to_read = std::min(avail, chunk_frames);
    if (scratch_left_.size() < to_read) {
        scratch_left_.resize(to_read);
        scratch_right_.resize(to_read);
        scratch_interleaved_.resize(to_read * std::max<std::uint16_t>(1, info_.channels));
    }

    sf_count_t frames_read = 0;
    if (info_.channels == 1) {
        frames_read = sf_read_float(handle, scratch_left_.data(), static_cast<sf_count_t>(to_read));
        if (frames_read > 0) {
            std::copy_n(scratch_left_.begin(), frames_read, scratch_right_.begin());
        }
    } else {
        frames_read = sf_readf_float(handle, scratch_interleaved_.data(), static_cast<sf_count_t>(to_read));
        if (frames_read > 0) {
            const std::size_t channels = info_.channels;
            for (sf_count_t i = 0; i < frames_read; ++i) {
                scratch_left_[static_cast<std::size_t>(i)] = scratch_interleaved_[static_cast<std::size_t>(i * channels)];
                scratch_right_[static_cast<std::size_t>(i)] = scratch_interleaved_[static_cast<std::size_t>(i * channels + 1)];
            }
        }
    }

    if (frames_read > 0) {
        const auto read_u = static_cast<std::size_t>(frames_read);
        ring_.write(std::span<const float>(scratch_left_.data(), read_u),
                    std::span<const float>(scratch_right_.data(), read_u));
        current_file_frame_.fetch_add(read_u, std::memory_order_relaxed);
        return true;
    }

    return false; // EOF reached
}

DiskStreamService::DiskStreamService(std::size_t thread_count) {
    const std::size_t count = std::max<std::size_t>(1, thread_count);
    workers_.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        workers_.emplace_back(&DiskStreamService::worker_loop, this);
    }
}

DiskStreamService::~DiskStreamService() {
    stop_.store(true, std::memory_order_release);
    cv_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) worker.join();
    }
}

std::shared_ptr<DiskStream> DiskStreamService::open_stream(const std::filesystem::path& path,
                                                          std::size_t ring_capacity) {
    auto stream = std::make_shared<DiskStream>(path, ring_capacity);
    if (stream->is_open()) {
        register_stream(stream);
    }
    return stream;
}

void DiskStreamService::register_stream(std::shared_ptr<DiskStream> stream) {
    std::lock_guard lock(mutex_);
    streams_.push_back(std::move(stream));
    cv_.notify_all();
}

void DiskStreamService::unregister_stream(const std::shared_ptr<DiskStream>& stream) {
    std::lock_guard lock(mutex_);
    std::erase(streams_, stream);
}

void DiskStreamService::notify() {
    cv_.notify_all();
}

void DiskStreamService::prefill(std::size_t target_frames) {
    std::vector<std::shared_ptr<DiskStream>> active;
    {
        std::lock_guard lock(mutex_);
        active = streams_;
    }
    for (const auto& stream : active) {
        if (!stream) continue;
        while (stream->ring_buffer().available_read() < target_frames) {
            if (!stream->fill_buffer(target_frames)) {
                break; // EOF or error
            }
        }
    }
}

void DiskStreamService::worker_loop() {
    while (!stop_.load(std::memory_order_acquire)) {
        std::vector<std::shared_ptr<DiskStream>> active;
        {
            std::unique_lock lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(10), [this] {
                return stop_.load(std::memory_order_acquire);
            });
            active = streams_;
        }

        bool did_work = false;
        for (const auto& stream : active) {
            if (stream && stream->ring_buffer().available_write() >= 1024) {
                if (stream->fill_buffer()) {
                    did_work = true;
                }
            }
        }

        if (!did_work && !stop_.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}

} // namespace blokkily
