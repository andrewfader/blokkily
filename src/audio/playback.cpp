#include "blokkily/audio/playback.hpp"

#include "blokkily/audio/event_timeline.hpp"
#include "blokkily/sequencer/scheduler.hpp"

#include <RtAudio.h>

#include <algorithm>
#include <array>
#include <cmath>

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
    if (!instrument_ || bpm <= 0.0 || sample_rate <= 0.0 || maximum_block_size == 0) return false;
    if (!instrument_->activate(sample_rate, 1, maximum_block_size)) return false;
    const double samples_per_tick = sample_rate * 60.0 /
                                    (bpm * static_cast<double>(pattern.ticks_per_beat()));
    loop_samples_ = static_cast<std::uint64_t>(std::llround(pattern.length() * samples_per_tick));
    if (loop_samples_ == 0) return false;
    // The song engine compiles its timeline the same way, so pattern preview
    // and arrangement playback can never disagree about when a step sounds.
    events_ = compile_timeline(Scheduler{}.render(pattern, 1, seed), samples_per_tick,
                               loop_samples_ - 1);
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
    if (!is_playing() || loop_samples_ == 0) {
        std::fill(output.left.begin(), output.left.end(), 0.0F);
        std::fill(output.right.begin(), output.right.end(), 0.0F);
        return;
    }
    std::size_t rendered = 0;
    while (rendered < output.left.size()) {
        const auto loop_position = sample_position_ % loop_samples_;
        const auto until_wrap = loop_samples_ - loop_position;
        const auto frames = std::min<std::uint64_t>(output.left.size() - rendered, until_wrap);
        process_chunk({output.left.subspan(rendered, frames), output.right.subspan(rendered, frames)},
                      loop_position);
        rendered += frames;
        sample_position_ += frames;
    }
}

struct RtAudioOutput::Impl {
    RtAudio audio;
    AudioSource* source = nullptr;

    static int callback(void* output, void*, unsigned int frames, double,
                        RtAudioStreamStatus, void* user) {
        auto* self = static_cast<Impl*>(user);
        auto* samples = static_cast<float*>(output);
        self->source->process({std::span{samples, frames}, std::span{samples + frames, frames}});
        return 0;
    }
};

RtAudioOutput::RtAudioOutput() : impl_(std::make_unique<Impl>()) {}
RtAudioOutput::~RtAudioOutput() { stop(); if (impl_->audio.isStreamOpen()) impl_->audio.closeStream(); }

bool RtAudioOutput::open(AudioSource& source, unsigned int sample_rate,
                         unsigned int requested_frames, std::string* error) {
    if (impl_->audio.getDeviceCount() == 0) {
        if (error) *error = "no audio output device found";
        return false;
    }
    RtAudio::StreamParameters parameters;
    parameters.deviceId = impl_->audio.getDefaultOutputDevice();
    parameters.nChannels = 2;
    parameters.firstChannel = 0;
    RtAudio::StreamOptions options;
    options.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_MINIMIZE_LATENCY;
    impl_->source = &source;
    const auto result = impl_->audio.openStream(&parameters, nullptr, RTAUDIO_FLOAT32,
        sample_rate, &requested_frames, &Impl::callback, impl_.get(), &options);
    if (result != RTAUDIO_NO_ERROR) {
        if (error) *error = impl_->audio.getErrorText();
        return false;
    }
    return true;
}

bool RtAudioOutput::start(std::string* error) {
    const auto result = impl_->audio.startStream();
    if (result == RTAUDIO_NO_ERROR) return true;
    if (error) *error = impl_->audio.getErrorText();
    return false;
}

void RtAudioOutput::stop() noexcept {
    if (impl_ && impl_->audio.isStreamRunning()) (void)impl_->audio.stopStream();
}
void RtAudioOutput::rebind(AudioSource& source) noexcept {
    if (impl_) impl_->source = &source;
}
bool RtAudioOutput::is_open() const noexcept { return impl_ && impl_->audio.isStreamOpen(); }

} // namespace blokkily
