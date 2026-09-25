#include "blokkily/audio/playback.hpp"

#include "blokkily/audio/event_timeline.hpp"
#include "blokkily/sequencer/scheduler.hpp"

#include <RtAudio.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <vector>

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
    // How many input channels the callback is handed: what the duplex stream
    // opened with, or what the deterministic device models.
    unsigned int input_channels = 0;
    std::uint32_t round_trip = 0;
    bool want_input = false;
    bool opened_wanting_input = false;
    // The deterministic device's loopback cable: what each output channel
    // played over the last `round_trip` frames, and the scratch a piece of a
    // block is rendered through.
    bool loopback = false;
    std::uint64_t pumped_frames = 0;
    std::array<std::vector<float>, 2> played;
    std::vector<float> input;
    std::vector<float> piece;

    static int callback(void* output, void* input, unsigned int frames, double,
                        RtAudioStreamStatus, void* user) {
        auto* self = static_cast<Impl*>(user);
        auto* samples = static_cast<float*>(output);
        self->callbacks.fetch_add(1, std::memory_order_relaxed);
        unsigned int seen = self->largest.load(std::memory_order_relaxed);
        while (frames > seen &&
               !self->largest.compare_exchange_weak(seen, frames, std::memory_order_relaxed)) {
        }
        // RtAudio hands a duplex stream's input non-interleaved, channel by
        // channel, exactly as InputBlock reads it.
        InputBlock block;
        if (input != nullptr && self->input_channels > 0)
            block = {static_cast<const float*>(input), self->input_channels, frames, frames};
        self->source->process({std::span{samples, frames}, std::span{samples + frames, frames}},
                              block);
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
    impl_->input_channels = 0;
    impl_->info.input_device.clear();
    // Duplex first, so a track can record what is plugged into the inputs
    // (item 3.2). A server that refuses it - a capture device at another rate,
    // one that is busy - still gets an output-only stream: playing never
    // depends on recording being possible.
    unsigned int frames = requested_frames;
    auto result = RTAUDIO_INVALID_USE;
    const auto input_device = impl_->want_input ? impl_->audio->getDefaultInputDevice() : 0U;
    const auto input_info = input_device != 0 ? impl_->audio->getDeviceInfo(input_device)
                                              : RtAudio::DeviceInfo{};
    impl_->opened_wanting_input = impl_->want_input;
    if (impl_->want_input && input_device != 0 && input_info.inputChannels > 0) {
        RtAudio::StreamParameters input;
        input.deviceId = input_device;
        // A pair is what a track records at most; a few more let a track pick
        // its pair on an interface with several.
        input.nChannels = std::min(input_info.inputChannels, 8U);
        input.firstChannel = 0;
        result = impl_->audio->openStream(&parameters, &input, RTAUDIO_FLOAT32, sample_rate,
                                          &frames, &Impl::callback, impl_.get(), &options);
        if (result == RTAUDIO_NO_ERROR) {
            impl_->input_channels = input.nChannels;
            impl_->info.input_device = input_info.name;
        } else if (impl_->audio->isStreamOpen()) {
            impl_->audio->closeStream();
        }
    }
    if (result != RTAUDIO_NO_ERROR) {
        frames = requested_frames;
        result = impl_->audio->openStream(&parameters, nullptr, RTAUDIO_FLOAT32, sample_rate,
                                          &frames, &Impl::callback, impl_.get(), &options);
    }
    requested_frames = frames;
    if (result != RTAUDIO_NO_ERROR) {
        if (error) *error = impl_->audio->getErrorText();
        return false;
    }
    impl_->info.input_channels = impl_->input_channels;
    impl_->round_trip = static_cast<std::uint32_t>(std::max(0L, impl_->audio->getStreamLatency()));
    impl_->info.round_trip_frames = impl_->round_trip;
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
    // A device with inputs delivers them whether or not anything is played
    // into them: silence, or what the loopback cable carries.
    if (impl_->input_channels > 0) {
        try {
            return pump(stereo, {});
        } catch (...) {
            return false;
        }
    }
    return Impl::callback(stereo.data(), nullptr, static_cast<unsigned int>(stereo.size() / 2),
                          0.0, 0, impl_.get()) == 0;
}

void RtAudioOutput::model_input(unsigned int channels, std::uint32_t round_trip, bool loopback) {
    if (!impl_->deterministic) return;
    impl_->input_channels = channels;
    impl_->round_trip = round_trip;
    impl_->loopback = loopback && round_trip > 0 && channels > 0;
    impl_->pumped_frames = 0;
    for (auto& line : impl_->played) line.assign(impl_->loopback ? round_trip : 0, 0.0F);
    impl_->info.input_device = channels > 0 ? "Deterministic input" : "";
    impl_->info.input_channels = channels;
    impl_->info.round_trip_frames = round_trip;
}

bool RtAudioOutput::pump(std::span<float> stereo, std::span<const float> injected) {
    if (!impl_->deterministic || !is_running() || stereo.empty() || stereo.size() % 2 != 0)
        return false;
    const auto frames = stereo.size() / 2;
    const auto channels = static_cast<std::size_t>(impl_->input_channels);
    if (!injected.empty() && injected.size() != channels * frames) return false;
    if (channels == 0)
        return Impl::callback(stereo.data(), nullptr, static_cast<unsigned int>(frames), 0.0, 0,
                              impl_.get()) == 0;
    // With a cable from the outputs, a piece is never longer than the round
    // trip: every input frame it needs was played before the piece began.
    const std::size_t longest = impl_->loopback ? impl_->round_trip : frames;
    for (std::size_t done = 0; done < frames;) {
        const auto count = std::min(longest, frames - done);
        impl_->input.assign(channels * count, 0.0F);
        impl_->piece.assign(2 * count, 0.0F);
        for (std::size_t channel = 0; channel < channels; ++channel) {
            auto* heard = impl_->input.data() + channel * count;
            if (!injected.empty())
                std::copy_n(injected.data() + channel * frames + done, count, heard);
            if (!impl_->loopback) continue;
            const auto& line = impl_->played[channel % 2];
            for (std::size_t frame = 0; frame < count; ++frame)
                heard[frame] += line[(impl_->pumped_frames + frame) % line.size()];
        }
        if (Impl::callback(impl_->piece.data(), impl_->input.data(),
                           static_cast<unsigned int>(count), 0.0, 0, impl_.get()) != 0)
            return false;
        for (std::size_t side = 0; side < 2; ++side) {
            const auto* played = impl_->piece.data() + side * count;
            std::copy_n(played, count, stereo.data() + side * frames + done);
            if (!impl_->loopback) continue;
            auto& line = impl_->played[side];
            for (std::size_t frame = 0; frame < count; ++frame)
                line[(impl_->pumped_frames + frame) % line.size()] = played[frame];
        }
        impl_->pumped_frames += count;
        done += count;
    }
    return true;
}

void RtAudioOutput::set_input_wanted(bool wanted) noexcept { impl_->want_input = wanted; }

bool RtAudioOutput::opened_for_input(bool wanted) const noexcept {
    return impl_->deterministic || !is_open() || impl_->opened_wanting_input == wanted;
}

void RtAudioOutput::close() noexcept {
    if (impl_->deterministic) return;
    stop();
    if (impl_->audio->isStreamOpen()) impl_->audio->closeStream();
    impl_->input_channels = 0;
    impl_->round_trip = 0;
    impl_->info = {};
}

unsigned int RtAudioOutput::input_channels() const noexcept { return impl_->input_channels; }

std::uint32_t RtAudioOutput::round_trip_latency() const noexcept { return impl_->round_trip; }

} // namespace blokkily
