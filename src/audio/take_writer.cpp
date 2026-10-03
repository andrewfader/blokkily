#include "blokkily/audio/take_writer.hpp"

#include "blokkily/audio/audio_clips.hpp"
#include "blokkily/audio/sndfile_path.hpp"

#include <sndfile.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <system_error>

namespace blokkily {

// One take being written: its file, where it began, where the next chunk has
// to begin to belong to it, and the audio so far.
struct TakeWriter::Segment {
    RecordedTake take;
    SNDFILE* handle = nullptr;
    std::uint64_t next_sample = 0;
    std::vector<float> left;
    std::vector<float> right;
};

TakeWriter::TakeWriter(std::size_t ring_samples) : ring_(ring_samples) {}

TakeWriter::~TakeWriter() { (void)finish(); }

void TakeWriter::begin(const std::filesystem::path& directory, std::uint32_t sample_rate,
                       bool threaded) {
    if (active_) return;
    // Whatever was captured while no take was being written belongs to no
    // take: it is thrown away rather than written into this one.
    CaptureHeader header;
    while (ring_.pop(header, scratch_)) {
    }
    directory_ = directory;
    sample_rate_ = sample_rate;
    dropped_at_begin_ = ring_.dropped_frames();
    finished_.clear();
    active_ = true;
    stop_.store(false, std::memory_order_release);
    if (threaded) thread_ = std::thread([this] { run(); });
}

void TakeWriter::run() {
    // Written off the audio thread and off the interface thread: a file
    // system that stalls holds up neither.
    while (!stop_.load(std::memory_order_acquire)) {
        drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
}

void TakeWriter::drain() {
    CaptureHeader header;
    while (ring_.pop(header, scratch_)) write_chunk(header, scratch_);
}

std::uint64_t TakeWriter::dropped_frames() const noexcept {
    return ring_.dropped_frames() - dropped_at_begin_;
}

void TakeWriter::write_chunk(const CaptureHeader& header, const std::vector<float>& samples) {
    if (header.frames == 0 || header.channels == 0) return;
    if (open_.size() <= header.track) open_.resize(header.track + 1);
    auto& open = open_[header.track];
    // A chunk that does not carry on from the last one - a loop wrap, a seek,
    // frames the ring dropped, another channel count - ends the take and
    // starts another, so neither is shifted in time.
    if (open && (open->next_sample != header.song_sample ||
                 open->take.channels != header.channels))
        close_segment(header.track);
    if (!open) {
        open = std::make_unique<Segment>();
        auto& take = open->take;
        take.track = header.track;
        take.first_sample = header.song_sample;
        take.channels = header.channels;
        take.sample_rate = sample_rate_;
        std::error_code ignored;
        std::filesystem::create_directories(directory_, ignored);
        do {
            take.file = directory_ / ("take-" + std::to_string(header.track + 1) + "-" +
                                      std::to_string(next_number_++) + ".wav");
        } while (std::filesystem::exists(take.file, ignored));
        SF_INFO info{};
        info.samplerate = static_cast<int>(sample_rate_);
        info.channels = header.channels;
        info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
        open->handle = sf_open(sndfile_path(take.file).c_str(), SFM_WRITE, &info);
        if (open->handle == nullptr) take.error = sf_strerror(nullptr);
        open->next_sample = header.song_sample;
    }
    auto& segment = *open;
    const std::size_t frames = header.frames;
    const auto first = samples.begin();
    segment.left.insert(segment.left.end(), first, first + static_cast<std::ptrdiff_t>(frames));
    if (header.channels == 2)
        segment.right.insert(segment.right.end(), first + static_cast<std::ptrdiff_t>(frames),
                             first + static_cast<std::ptrdiff_t>(2 * frames));
    if (segment.handle != nullptr) {
        interleaved_.resize(frames * header.channels);
        for (std::size_t frame = 0; frame < frames; ++frame)
            for (std::size_t channel = 0; channel < header.channels; ++channel)
                interleaved_[frame * header.channels + channel] = samples[channel * frames + frame];
        if (sf_writef_float(segment.handle, interleaved_.data(),
                            static_cast<sf_count_t>(frames)) != static_cast<sf_count_t>(frames))
            segment.take.error = sf_strerror(segment.handle);
    }
    segment.take.frames += frames;
    segment.next_sample += frames;
}

void TakeWriter::close_segment(std::size_t track) {
    if (track >= open_.size() || !open_[track]) return;
    auto segment = std::move(open_[track]);
    if (segment->handle != nullptr && sf_close(segment->handle) != 0 && segment->take.error.empty())
        segment->take.error = "the take's file could not be closed";
    auto asset = std::make_shared<AudioAsset>();
    asset->rate = segment->take.sample_rate;
    asset->frames = segment->take.frames;
    asset->left = std::move(segment->left);
    asset->right = std::move(segment->right);
    asset->peaks = compute_peaks(asset->left, asset->right);
    segment->take.audio = std::move(asset);
    finished_.push_back(std::move(segment->take));
}

std::vector<RecordedTake> TakeWriter::finish() {
    if (!active_) return {};
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
    drain();
    for (std::size_t track = 0; track < open_.size(); ++track) close_segment(track);
    open_.clear();
    active_ = false;
    std::stable_sort(finished_.begin(), finished_.end(),
                     [](const RecordedTake& a, const RecordedTake& b) {
                         return a.first_sample < b.first_sample;
                     });
    return std::exchange(finished_, {});
}

std::optional<TakePlacement> place_take(const TickClock& clock, std::uint64_t first_sample,
                                        std::uint64_t frames, std::uint64_t compensation) {
    // Where the take's first frame was heard. Frames heard before the song
    // began are cut off rather than moved.
    std::uint64_t skipped = 0;
    std::uint64_t heard = 0;
    if (first_sample >= compensation) heard = first_sample - compensation;
    else skipped = compensation - first_sample;
    // A clip starts on a tick; the first tick at or after the heard sample is
    // where it goes, and the frames before that tick's sample are skipped, so
    // the audio lands on the sample it was heard at, not on the tick grid.
    Tick start = tick_at_sample(clock, heard);
    while (sample_for_tick(clock, static_cast<double>(start)) < heard) ++start;
    skipped += sample_for_tick(clock, static_cast<double>(start)) - heard;
    if (skipped >= frames) return std::nullopt;
    return TakePlacement{start, skipped, frames - skipped};
}

std::uint64_t take_compensation(std::uint64_t round_trip, std::uint64_t output_latency,
                                std::int64_t record_offset) noexcept {
    const auto total = static_cast<std::int64_t>(round_trip + output_latency) + record_offset;
    return total > 0 ? static_cast<std::uint64_t>(total) : 0;
}

std::optional<AudioClipId> commit_take(Song& song, const RecordedTake& take,
                                       const TickClock& clock, std::uint64_t compensation) {
    if (take.track >= song.tracks.size() || take.frames == 0 || take.sample_rate == 0 ||
        take.channels == 0 || !take.error.empty() || take.file.empty())
        return std::nullopt;
    const auto placed = place_take(clock, take.first_sample, take.frames, compensation);
    if (!placed) return std::nullopt;
    AudioClip clip;
    clip.id = song.next_audio_clip_id();
    clip.track = take.track;
    clip.file = add_audio_file(song, {take.file, take.frames, take.sample_rate, take.channels});
    clip.start = placed->start;
    clip.offset_frames = placed->offset_frames;
    clip.length_frames = placed->length_frames;
    song.audio_clips.push_back(clip);
    return clip.id;
}

} // namespace blokkily
