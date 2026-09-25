#include "blokkily/audio/playback.hpp"

#include "blokkily/audio/event_timeline.hpp"
#include "blokkily/sequencer/scheduler.hpp"

#include <RtAudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>

namespace blokkily {

RealtimePlayback::RealtimePlayback(std::unique_ptr<PluginInstance> instrument)
    : instrument_(std::move(instrument)) {}

std::vector<std::byte> RealtimePlayback::save_instrument_state() {
    return instrument_ ? instrument_->save_state() : std::vector<std::byte>{};
}

bool RealtimePlayback::load_instrument_state(std::span<const std::byte> state) {
    return instrument_ && instrument_->load_state(state);
}

bool RealtimePlayback::prepare(const Pattern& pattern, double bpm, double sample_rate,
                               std::uint32_t maximum_block_size, std::uint64_t seed) {
    if (!instrument_ || !std::isfinite(bpm) || !std::isfinite(sample_rate) ||
        bpm <= 0.0 || sample_rate <= 0.0 || maximum_block_size == 0) return false;
    const double samples_per_tick = sample_rate * 60.0 /
                                    (bpm * static_cast<double>(pattern.ticks_per_beat()));
    const double length = pattern.length() * samples_per_tick;
    if (!std::isfinite(length) || length < 0.5 ||
        length >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) return false;
    const auto loop_samples = static_cast<std::uint64_t>(std::llround(length));
    // The song engine compiles its timeline the same way, so pattern preview
    // and arrangement playback can never disagree about when a step sounds.
    auto events = compile_timeline(Scheduler{}.render(pattern, 1, seed), samples_per_tick,
                                   loop_samples - 1);
    if (!timeline_density_supported(events)) return false;
    if (!instrument_->activate(sample_rate, 1, maximum_block_size)) return false;
    loop_samples_ = loop_samples;
    events_ = std::move(events);
    maximum_block_ = maximum_block_size;
    sample_position_ = 0;
    return loop_samples_ > 0;
}

void RealtimePlayback::process_chunk(StereoBlock output, std::uint64_t loop_position) noexcept {
    constexpr std::size_t maximum_events_per_block = 256;
    std::array<PluginEvent, maximum_events_per_block> block_events{};
    std::size_t count = 0;
    const auto end = loop_position + output.left.size();
    for (const auto& timed : events_) {
        if (timed.sample < loop_position || timed.sample >= end) continue;
        if (count == block_events.size()) break;
        block_events[count] = timed.event;
        block_events[count].sample_offset = static_cast<std::uint32_t>(timed.sample - loop_position);
        ++count;
    }
    instrument_->process(output, std::span{block_events.data(), count});
}

void RealtimePlayback::process(StereoBlock output) noexcept {
    if (output.left.size() != output.right.size()) return;
    if (!is_playing() || loop_samples_ == 0 || maximum_block_ == 0) {
        std::fill(output.left.begin(), output.left.end(), 0.0F);
        std::fill(output.right.begin(), output.right.end(), 0.0F);
        return;
    }
    std::size_t rendered = 0;
    while (rendered < output.left.size()) {
        const auto loop_position = sample_position_ % loop_samples_;
        const auto until_wrap = loop_samples_ - loop_position;
        auto frames = std::min<std::uint64_t>(
            {output.left.size() - rendered, until_wrap, maximum_block_});
        frames = timeline_window(events_, loop_position, frames);
        process_chunk({output.left.subspan(rendered, frames), output.right.subspan(rendered, frames)},
                      loop_position);
        rendered += frames;
        sample_position_ += frames;
    }
}

struct RtAudioOutput::Impl {
    std::unique_ptr<RtAudio> audio;
    bool deterministic = false;
    unsigned int negotiated_rate = 0;
    bool running = false;
    AudioSource* source = nullptr;
    DeviceInfo info;
    // Written by the audio thread, read by whoever is asking. Counters only:
    // nothing here allocates, locks, or blocks the callback.
    std::atomic<std::uint64_t> callbacks{0};
    std::atomic<unsigned int> largest{0};

    static int callback(void* output, void*, unsigned int frames, double,
                        RtAudioStreamStatus, void* user) {
        auto* self = static_cast<Impl*>(user);
        auto* samples = static_cast<float*>(output);
        self->callbacks.fetch_add(1, std::memory_order_relaxed);
        unsigned int seen = self->largest.load(std::memory_order_relaxed);
        while (frames > seen &&
               !self->largest.compare_exchange_weak(seen, frames, std::memory_order_relaxed)) {
        }
        self->source->process({std::span{samples, frames}, std::span{samples + frames, frames}});
        return 0;
    }
};

RtAudioOutput::DeviceInfo RtAudioOutput::device_info() const { return impl_->info; }

std::uint64_t RtAudioOutput::callback_count() const noexcept {
    return impl_->callbacks.load(std::memory_order_relaxed);
}

unsigned int RtAudioOutput::largest_block() const noexcept {
    return impl_->largest.load(std::memory_order_relaxed);
}

RtAudioOutput::RtAudioOutput(Mode mode, unsigned int negotiated_rate)
    : impl_(std::make_unique<Impl>()) {
    impl_->deterministic = mode == Mode::deterministic;
    impl_->negotiated_rate = negotiated_rate;
    if (!impl_->deterministic) impl_->audio = std::make_unique<RtAudio>();
}
RtAudioOutput::~RtAudioOutput() {
    stop();
    if (impl_->audio && impl_->audio->isStreamOpen()) impl_->audio->closeStream();
}

bool RtAudioOutput::open(AudioSource& source, unsigned int sample_rate,
                         unsigned int requested_frames, std::string* error) {
    if (impl_->deterministic) {
        impl_->source = &source;
        // A deterministic host is a device as far as the engine is concerned:
        // it says which rate and which block the callback will be pumped at,
        // so the code that follows a server follows this the same way.
        impl_->info.api = "Deterministic";
        impl_->info.device = "Deterministic pump";
        impl_->info.sample_rate =
            impl_->negotiated_rate != 0 ? impl_->negotiated_rate : sample_rate;
        impl_->info.buffer_frames = requested_frames;
        return true;
    }
    if (impl_->audio->getDeviceCount() == 0) {
        if (error) *error = "no audio output device found";
        return false;
    }
    RtAudio::StreamParameters parameters;
    parameters.deviceId = impl_->audio->getDefaultOutputDevice();
    parameters.nChannels = 2;
    parameters.firstChannel = 0;
    RtAudio::StreamOptions options;
    options.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_MINIMIZE_LATENCY;
    impl_->source = &source;
    const auto result = impl_->audio->openStream(&parameters, nullptr, RTAUDIO_FLOAT32,
        sample_rate, &requested_frames, &Impl::callback, impl_.get(), &options);
    if (result != RTAUDIO_NO_ERROR) {
        if (error) *error = impl_->audio->getErrorText();
        return false;
    }
    // openStream writes the block size it actually negotiated back into
    // requested_frames, which is rarely what was asked for.
    impl_->info.api = RtAudio::getApiDisplayName(impl_->audio->getCurrentApi());
    impl_->info.device = impl_->audio->getDeviceInfo(parameters.deviceId).name;
    // The server decides the rate, not the caller: a device locked to 44.1 kHz
    // opens at the nearest rate it supports, and a song prepared for the rate
    // that was asked for would then play sharp and fast for ever. Report what
    // the open stream actually runs at, so the engine can be built for it.
    const auto negotiated = impl_->audio->getStreamSampleRate();
    impl_->info.sample_rate = negotiated != 0 ? negotiated : sample_rate;
    impl_->info.buffer_frames = requested_frames;
    return true;
}

bool RtAudioOutput::start(std::string* error) {
    if (is_running()) return true;
    if (impl_->deterministic) {
        impl_->running = is_open();
        return impl_->running;
    }
    const auto result = impl_->audio->startStream();
    if (result == RTAUDIO_NO_ERROR) return true;
    if (error) *error = impl_->audio->getErrorText();
    return false;
}

void RtAudioOutput::stop() noexcept {
    if (impl_->deterministic) impl_->running = false;
    else if (impl_->audio->isStreamRunning()) (void)impl_->audio->stopStream();
}
void RtAudioOutput::rebind(AudioSource& source) noexcept {
    if (impl_) impl_->source = &source;
}
bool RtAudioOutput::is_open() const noexcept {
    return impl_->deterministic ? impl_->source != nullptr : impl_->audio->isStreamOpen();
}
bool RtAudioOutput::is_running() const noexcept {
    return impl_->deterministic ? impl_->running : impl_->audio->isStreamRunning();
}
bool RtAudioOutput::pump(std::span<float> stereo) noexcept {
    if (!impl_->deterministic || !is_running() || stereo.empty() || stereo.size() % 2 != 0)
        return false;
    return Impl::callback(stereo.data(), nullptr, static_cast<unsigned int>(stereo.size() / 2),
                          0.0, 0, impl_.get()) == 0;
}

} // namespace blokkily
