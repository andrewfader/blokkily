#include "blokkily/audio/disk_stream.hpp"

#include "blokkily/audio/sndfile_path.hpp"

#include <samplerate.h>
#include <sndfile.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numeric>

namespace blokkily {

// --- StreamReader -------------------------------------------------------------

namespace {
constexpr std::size_t reader_chunk = 4096;
// Input frames decoded before a seek target when resampling, so the
// converter's filter is full of the file's own samples by the time it
// reaches the target: the converter then produces what it produces reading
// the file from its start, to within rounding.
constexpr std::uint64_t converter_preroll = 8192;
} // namespace

struct StreamReader::Impl {
    SNDFILE* file = nullptr;
    std::uint16_t channels = 0;
    std::uint64_t native_frames = 0;
    std::uint64_t total = 0;
    bool convert = false;
    double ratio = 1.0;
    // Output frame m of the converter sits at input time m / ratio; it sits
    // on an input frame exactly when m is a multiple of `p`, at input frame
    // m / p * q.
    std::uint64_t p = 1, q = 1;
    SRC_STATE* converter[2] = {nullptr, nullptr};

    std::vector<float> interleaved;
    std::vector<float> in[2];
    std::size_t in_count = 0, in_used = 0;
    bool input_done = false;
    bool drained = false;
    std::vector<float> out[2];
    std::size_t out_count = 0, out_head = 0;
    std::uint64_t out_index = 0;   // engine frame of out[.][out_head]
    std::uint64_t position = 0;    // the next frame read() delivers

    ~Impl() {
        for (auto* state : converter)
            if (state != nullptr) src_delete(state);
        if (file != nullptr) sf_close(file);
    }

    // Reads the next chunk of the file into `in`. False at its end.
    bool decode_chunk() {
        const auto got = sf_readf_float(file, interleaved.data(),
                                        static_cast<sf_count_t>(reader_chunk));
        if (got <= 0) return false;
        const auto frames = static_cast<std::size_t>(got);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            in[0][frame] = interleaved[frame * channels];
            if (channels >= 2) in[1][frame] = interleaved[frame * channels + 1];
        }
        in_count = frames;
        in_used = 0;
        return true;
    }

    // Runs the converter once; false when it has nothing more to give.
    bool convert_more() {
        if (drained) return false;
        if (in_used == in_count && !input_done && !decode_chunk()) input_done = true;
        const int used_channels = channels >= 2 ? 2 : 1;
        long used = 0;
        long generated = 0;
        for (int channel = 0; channel < used_channels; ++channel) {
            SRC_DATA data{};
            data.data_in = in[channel].data() + in_used;
            data.input_frames = static_cast<long>(in_count - in_used);
            data.data_out = out[channel].data();
            data.output_frames = static_cast<long>(out[channel].size());
            data.end_of_input = input_done ? 1 : 0;
            data.src_ratio = ratio;
            if (src_process(converter[channel], &data) != 0) {
                drained = true;
                return false;
            }
            used = data.input_frames_used;
            generated = channel == 0 ? data.output_frames_gen
                                     : std::min(generated, data.output_frames_gen);
        }
        in_used += static_cast<std::size_t>(used);
        out_head = 0;
        out_count = static_cast<std::size_t>(generated);
        if (generated == 0 && used == 0 && input_done) drained = true;
        return true;
    }
};

StreamReader::StreamReader() : impl_(std::make_unique<Impl>()) {}
StreamReader::~StreamReader() = default;

bool StreamReader::is_open() const noexcept { return impl_->file != nullptr; }

bool StreamReader::open(const std::filesystem::path& path, double engine_rate,
                        std::string* error) {
    impl_ = std::make_unique<Impl>();
    SF_INFO info{};
    SNDFILE* file = sf_open(sndfile_path(path).c_str(), SFM_READ, &info);
    if (file == nullptr) {
        if (error != nullptr) *error = "cannot open " + path.string() + ": " + sf_strerror(nullptr);
        return false;
    }
    // Normalised exactly as decode_audio_file() reads it.
    sf_command(file, SFC_SET_NORM_FLOAT, nullptr, SF_TRUE);
    auto& impl = *impl_;
    impl.file = file;
    native_ = {static_cast<std::uint64_t>(std::max<sf_count_t>(0, info.frames)),
               static_cast<std::uint32_t>(std::max(0, info.samplerate)),
               static_cast<std::uint16_t>(std::max(0, info.channels))};
    if (native_.sample_rate == 0 || native_.channels == 0) {
        if (error != nullptr) *error = path.string() + " has no audio";
        impl_ = std::make_unique<Impl>();
        return false;
    }
    impl.channels = native_.channels;
    impl.native_frames = native_.frames;
    const auto engine = static_cast<std::uint32_t>(std::lround(engine_rate));
    impl.convert = engine != 0 && engine != native_.sample_rate;
    impl.ratio = impl.convert ? static_cast<double>(engine) / native_.sample_rate : 1.0;
    impl.total = impl.convert ? static_cast<std::uint64_t>(std::llround(
                                    static_cast<double>(native_.frames) * impl.ratio))
                              : native_.frames;
    frames_ = impl.total;
    impl.interleaved.assign(reader_chunk * impl.channels, 0.0F);
    impl.in[0].assign(reader_chunk, 0.0F);
    impl.in[1].assign(reader_chunk, 0.0F);
    if (impl.convert) {
        const auto divisor = std::gcd(engine, native_.sample_rate);
        impl.p = engine / divisor;
        impl.q = native_.sample_rate / divisor;
        const auto room = static_cast<std::size_t>(std::ceil(reader_chunk * impl.ratio)) + 64;
        for (int channel = 0; channel < 2; ++channel) {
            int status = 0;
            impl.converter[channel] = src_new(SRC_SINC_BEST_QUALITY, 1, &status);
            if (impl.converter[channel] == nullptr) {
                if (error != nullptr) *error = "cannot create a sample-rate converter";
                impl_ = std::make_unique<Impl>();
                return false;
            }
            impl.out[channel].assign(room, 0.0F);
        }
    }
    seek(0);
    return true;
}

void StreamReader::seek(std::uint64_t frame) {
    auto& impl = *impl_;
    if (impl.file == nullptr) return;
    impl.position = frame;
    if (!impl.convert) {
        sf_seek(impl.file, static_cast<sf_count_t>(std::min(frame, impl.native_frames)),
                SEEK_SET);
        return;
    }
    // Start the converter on an input frame that an output frame sits on
    // exactly, far enough before the target for its filter to be full.
    const auto target_input = static_cast<std::uint64_t>(static_cast<double>(frame) / impl.ratio);
    std::uint64_t start_input = 0;
    if (target_input > converter_preroll + impl.q)
        start_input = (target_input - converter_preroll) / impl.q * impl.q;
    const std::uint64_t start_output = start_input / impl.q * impl.p;
    for (auto* state : impl.converter) src_reset(state);
    sf_seek(impl.file, static_cast<sf_count_t>(std::min(start_input, impl.native_frames)),
            SEEK_SET);
    impl.in_count = impl.in_used = 0;
    impl.input_done = false;
    impl.drained = false;
    impl.out_count = impl.out_head = 0;
    impl.out_index = start_output;
}

void StreamReader::read(float* left, float* right, std::size_t count) {
    auto& impl = *impl_;
    std::size_t done = 0;
    const auto silence = [&](std::size_t frames) {
        std::fill_n(left + done, frames, 0.0F);
        std::fill_n(right + done, frames, 0.0F);
        done += frames;
        impl.position += frames;
    };
    if (impl.file == nullptr) {
        silence(count);
        return;
    }
    if (!impl.convert) {
        while (done < count) {
            if (impl.position >= impl.native_frames) {
                silence(count - done);
                break;
            }
            const auto wanted = static_cast<std::size_t>(std::min<std::uint64_t>(
                {count - done, impl.native_frames - impl.position, reader_chunk}));
            const auto got = sf_readf_float(impl.file, impl.interleaved.data(),
                                            static_cast<sf_count_t>(wanted));
            const auto frames = got > 0 ? static_cast<std::size_t>(got) : 0;
            for (std::size_t frame = 0; frame < frames; ++frame) {
                const float l = impl.interleaved[frame * impl.channels];
                left[done + frame] = l;
                right[done + frame] =
                    impl.channels >= 2 ? impl.interleaved[frame * impl.channels + 1] : l;
            }
            done += frames;
            impl.position += frames;
            // A file shorter than its header says reads as silence.
            if (frames < wanted) {
                impl.position = impl.native_frames;
                silence(count - done);
                break;
            }
        }
        return;
    }
    const bool stereo = impl.channels >= 2;
    while (done < count) {
        if (impl.position >= impl.total) {
            silence(count - done);
            break;
        }
        if (impl.out_head < impl.out_count) {
            if (impl.out_index < impl.position) {
                const auto skip = static_cast<std::size_t>(std::min<std::uint64_t>(
                    impl.out_count - impl.out_head, impl.position - impl.out_index));
                impl.out_head += skip;
                impl.out_index += skip;
                continue;
            }
            const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>(
                {impl.out_count - impl.out_head, count - done, impl.total - impl.position}));
            std::copy_n(impl.out[0].data() + impl.out_head, frames, left + done);
            std::copy_n((stereo ? impl.out[1] : impl.out[0]).data() + impl.out_head, frames,
                        right + done);
            impl.out_head += frames;
            impl.out_index += frames;
            impl.position += frames;
            done += frames;
            continue;
        }
        if (!impl.convert_more()) {
            // resample() pads a converter that stops short with silence.
            const auto frames = static_cast<std::size_t>(
                std::min<std::uint64_t>(count - done, impl.total - impl.position));
            silence(frames);
            impl.out_index = impl.position;
        }
    }
}

// --- AudioStreamRingBuffer ----------------------------------------------------

AudioStreamRingBuffer::AudioStreamRingBuffer(std::size_t capacity_frames)
    : capacity_(std::max<std::size_t>(1024, capacity_frames)),
      left_(capacity_, 0.0F),
      right_(capacity_, 0.0F) {}

std::size_t AudioStreamRingBuffer::available_read() const noexcept {
    const std::uint64_t w = write_pos_.load(std::memory_order_acquire);
    const std::uint64_t r = read_pos_.load(std::memory_order_acquire);
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
    const std::size_t count = std::min({left.size(), right.size(), available_write()});
    if (count == 0) return 0;
    const std::uint64_t w = write_pos_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < count; ++i) {
        const auto index = static_cast<std::size_t>((w + i) % capacity_);
        left_[index] = left[i];
        right_[index] = right[i];
    }
    write_pos_.store(w + count, std::memory_order_release);
    return count;
}

std::size_t AudioStreamRingBuffer::read(std::span<float> left, std::span<float> right) noexcept {
    const std::size_t requested = std::min(left.size(), right.size());
    const std::size_t count = std::min(requested, available_read());
    const std::uint64_t r = read_pos_.load(std::memory_order_relaxed);
    for (std::size_t i = 0; i < count; ++i) {
        const auto index = static_cast<std::size_t>((r + i) % capacity_);
        left[i] = left_[index];
        right[i] = right_[index];
    }
    std::fill(left.begin() + static_cast<std::ptrdiff_t>(count),
              left.begin() + static_cast<std::ptrdiff_t>(requested), 0.0F);
    std::fill(right.begin() + static_cast<std::ptrdiff_t>(count),
              right.begin() + static_cast<std::ptrdiff_t>(requested), 0.0F);
    read_pos_.store(r + count, std::memory_order_release);
    return count;
}

float AudioStreamRingBuffer::left_at(std::size_t offset) const noexcept {
    return left_[static_cast<std::size_t>((read_pos_.load(std::memory_order_relaxed) + offset) %
                                          capacity_)];
}

float AudioStreamRingBuffer::right_at(std::size_t offset) const noexcept {
    return right_[static_cast<std::size_t>((read_pos_.load(std::memory_order_relaxed) + offset) %
                                           capacity_)];
}

void AudioStreamRingBuffer::skip(std::size_t frames) noexcept {
    const auto count = std::min(frames, available_read());
    read_pos_.store(read_pos_.load(std::memory_order_relaxed) + count, std::memory_order_release);
}

void AudioStreamRingBuffer::skip_to(std::uint64_t position) noexcept {
    if (position > read_pos_.load(std::memory_order_relaxed))
        read_pos_.store(position, std::memory_order_release);
}

// --- DiskStream ---------------------------------------------------------------

DiskStream::DiskStream(const std::filesystem::path& path, double engine_rate,
                       std::uint64_t first_frame, std::size_t ring_capacity)
    : path_(path), ring_(ring_capacity) {
    open_ = reader_.open(path, engine_rate);
    frames_ = reader_.frames();
    first_frame &= frame_mask;
    reader_.seek(first_frame);
    request_.store(pack(0, first_frame), std::memory_order_relaxed);
    served_.store(pack(0, first_frame), std::memory_order_relaxed);
    requested_frame_ = read_frame_ = want_ = first_frame;
    scratch_left_.assign(reader_chunk, 0.0F);
    scratch_right_.assign(reader_chunk, 0.0F);
}

DiskStream::~DiskStream() = default;

void DiskStream::request(std::uint64_t frame) noexcept {
    generation_ = (generation_ + 1) & 0xFFFFFFU;
    requested_frame_ = frame;
    synced_ = false;
    request_.store(pack(generation_, frame), std::memory_order_release);
}

void DiskStream::poll() noexcept {
    if (synced_) return;
    const auto served = served_.load(std::memory_order_acquire);
    if ((served >> frame_bits) != generation_) return;
    // The generation's data begins where the producer was writing when it
    // served the request; everything before that is the old position's.
    const auto start = generation_start_.load(std::memory_order_acquire);
    const auto at = ring_.read_position();
    ring_.skip_to(start);
    read_frame_ = requested_frame_ + (at > start ? at - start : 0);
    synced_ = true;
}

std::size_t DiskStream::play(std::span<float> left, std::span<float> right, std::size_t at,
                             std::size_t count, float step) noexcept {
    std::size_t consumed = 0;
    for (; consumed < count; ++consumed) {
        if (step < 0.0F && gain_ <= 0.0F) break;
        gain_ = std::clamp(gain_ + step, 0.0F, 1.0F);
        left[at + consumed] = ring_.left_at(consumed) * gain_;
        right[at + consumed] = ring_.right_at(consumed) * gain_;
    }
    ring_.skip(consumed);
    return consumed;
}

void DiskStream::read(std::uint64_t frame, std::span<float> left, std::span<float> right,
                      bool blocking) noexcept {
    const std::size_t count = std::min(left.size(), right.size());
    if (count == 0) return;
    poll();
    if (blocking) {
        // Offline: the frames are made here, on this thread, before they are
        // read, so nothing is ever missing and nothing fades.
        if (!(synced_ && read_frame_ == frame)) {
            if (synced_ && frame > read_frame_ && frame - read_frame_ <= buffered()) {
                ring_.skip(static_cast<std::size_t>(frame - read_frame_));
                read_frame_ = frame;
            } else {
                request(frame);
            }
        }
        for (int round = 0; round < 1024; ++round) {
            poll();
            if (synced_ && buffered() >= count) break;
            (void)fill();
        }
        gain_ = 1.0F;
        const auto got = ring_.read(left.first(count), right.first(count));
        read_frame_ += got;
        want_ = frame + count;
        return;
    }

    std::size_t out = 0;
    const float step = 1.0F / static_cast<float>(fade_frames);
    // The playhead jumped (a locate, a loop wrap, a clip moved under it):
    // what was playing fades out over the audio that would have followed it.
    if (frame != want_ && !(synced_ && read_frame_ == frame)) {
        if (gain_ > 0.0F && synced_) {
            out = play(left, right, 0, std::min(count, buffered()), -step);
            read_frame_ += out;
        }
        gain_ = 0.0F;
    }
    want_ = frame + count;
    const std::uint64_t target = frame + out;
    if (!(synced_ && read_frame_ == target)) {
        if (synced_ && target > read_frame_ && target - read_frame_ < ring_.capacity()) {
            // Behind: what the ring holds up to the target is late; drop it
            // and come back in with a fade.
            const auto late = std::min<std::uint64_t>(target - read_frame_, buffered());
            ring_.skip(static_cast<std::size_t>(late));
            read_frame_ += late;
            gain_ = 0.0F;
        } else if (!synced_ && target >= requested_frame_ &&
                   target - requested_frame_ < ring_.capacity() / 2) {
            // A request on its way already covers this position.
        } else {
            request(want_);
        }
    }
    const std::size_t remaining = count - out;
    if (synced_ && read_frame_ == target) {
        const auto available = buffered();
        if (available >= remaining + fade_frames) {
            const auto played = play(left, right, out, remaining, step);
            read_frame_ += played;
            out += played;
        } else {
            // Starving: fade out over the reserve instead of cutting.
            underrun_.store(true, std::memory_order_relaxed);
            if (gain_ > 0.0F) {
                const auto played = play(left, right, out, std::min(remaining, available), -step);
                read_frame_ += played;
                out += played;
            }
            gain_ = 0.0F;
        }
    } else {
        underrun_.store(true, std::memory_order_relaxed);
        gain_ = 0.0F;
    }
    std::fill(left.begin() + static_cast<std::ptrdiff_t>(out),
              left.begin() + static_cast<std::ptrdiff_t>(count), 0.0F);
    std::fill(right.begin() + static_cast<std::ptrdiff_t>(out),
              right.begin() + static_cast<std::ptrdiff_t>(count), 0.0F);
}

void DiskStream::cue(std::uint64_t frame) noexcept {
    poll();
    // A clip starts at full level on its first frame, as an in-memory clip
    // does; only a stream that has to wait fades in.
    if ((synced_ && read_frame_ == frame) || (!synced_ && requested_frame_ == frame)) {
        want_ = frame;
        gain_ = 1.0F;
        return;
    }
    request(frame);
    want_ = frame;
    gain_ = 1.0F;
}

bool DiskStream::fill() {
    std::lock_guard lock(producer_mutex_);
    if (!open_) return false;
    bool worked = false;
    // Bounded: at most a ring's worth per call, so one stream cannot hold a
    // worker while the others starve.
    for (std::size_t round = 0; round < ring_.capacity() / reader_chunk + 1; ++round) {
        const auto wanted = request_.load(std::memory_order_acquire);
        const auto generation = static_cast<std::uint32_t>(wanted >> frame_bits);
        if (generation != producer_generation_) {
            reader_.seek(wanted & frame_mask);
            producer_generation_ = generation;
            generation_start_.store(ring_.write_position(), std::memory_order_release);
            served_.store(wanted, std::memory_order_release);
            worked = true;
        }
        const auto room = ring_.available_write();
        if (room < reader_chunk) break;
        reader_.read(scratch_left_.data(), scratch_right_.data(), reader_chunk);
        ring_.write(scratch_left_, scratch_right_);
        worked = true;
    }
    return worked;
}

// --- DiskStreamService --------------------------------------------------------

DiskStreamService::DiskStreamService(std::size_t thread_count) {
    workers_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i)
        workers_.emplace_back(&DiskStreamService::worker_loop, this);
}

DiskStreamService::~DiskStreamService() {
    {
        std::lock_guard lock(mutex_);
        stop_ = true;
    }
    cv_.notify_all();
    for (auto& worker : workers_)
        if (worker.joinable()) worker.join();
}

void DiskStreamService::register_stream(const std::shared_ptr<DiskStream>& stream) {
    std::lock_guard lock(mutex_);
    std::erase_if(streams_, [](const std::weak_ptr<DiskStream>& held) { return held.expired(); });
    streams_.push_back(stream);
}

std::vector<std::shared_ptr<DiskStream>> DiskStreamService::snapshot() {
    std::vector<std::shared_ptr<DiskStream>> active;
    std::lock_guard lock(mutex_);
    active.reserve(streams_.size());
    for (const auto& held : streams_)
        if (auto stream = held.lock()) active.push_back(std::move(stream));
    return active;
}

bool DiskStreamService::service() {
    bool worked = false;
    for (const auto& stream : snapshot()) worked = stream->fill() || worked;
    return worked;
}

void DiskStreamService::service_until_idle() {
    for (int round = 0; round < 64 && service(); ++round) {
    }
}

void DiskStreamService::worker_loop() {
    for (;;) {
        const bool worked = service();
        std::unique_lock lock(mutex_);
        if (stop_) return;
        // Polled: the render callback never signals, because waking a thread
        // can take a lock. A millisecond is far inside a ring's second.
        if (!worked) cv_.wait_for(lock, std::chrono::milliseconds(1), [this] { return stop_; });
        if (stop_) return;
    }
}

} // namespace blokkily
