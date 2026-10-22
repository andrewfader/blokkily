#include "blokkily/audio/playback.hpp"

#include "blokkily/audio/event_timeline.hpp"
#include "blokkily/sequencer/scheduler.hpp"

#include <RtAudio.h>

#if BLOKKILY_HAVE_JACK
#include <jack/jack.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
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
    // The audio system asked for that this build does not have.
    std::string missing_api;
    bool deterministic = false;
    unsigned int negotiated_rate = 0;
    bool running = false;
    AudioSource* source = nullptr;
    DeviceInfo info;
    // Written by the audio thread, read by whoever is asking. Counters only:
    // nothing here allocates, locks, or blocks the callback.
    std::atomic<std::uint64_t> callbacks{0};
    std::atomic<unsigned int> largest{0};
    // The worst share of a block's time the mix took since it was read, and
    // the dropouts (underruns, overruns, xruns) the server has reported.
    std::atomic<float> peak_load{0.0F};
    std::atomic<std::uint64_t> dropouts{0};
    // How many input channels the callback is handed: what the duplex stream
    // opened with, or what the deterministic device models.
    unsigned int input_channels = 0;
    std::uint32_t round_trip = 0;
    bool want_input = false;
    bool opened_wanting_input = false;
    // The devices asked for by name; empty is the default.
    std::string output_name;
    std::string input_name;
    // The deterministic device's loopback cable: what each output channel
    // played over the last `round_trip` frames, and the scratch a piece of a
    // block is rendered through.
    bool loopback = false;
    std::uint64_t pumped_frames = 0;
    std::array<std::vector<float>, 2> played;
    std::vector<float> input;
    std::vector<float> piece;

#if BLOKKILY_HAVE_JACK
    // JACK is driven directly rather than through RtAudio. RtAudio reads the
    // server's block size once, before its client is activated, and stops
    // processing for good when the server then calls with another size - as
    // PipeWire's JACK does whenever its graph runs at a quantum other than
    // its default. A block here is whatever the server hands over.
    bool use_jack = false;
    jack_client_t* jack = nullptr;
    std::array<jack_port_t*, 2> jack_outputs{};
    std::vector<jack_port_t*> jack_inputs;
    // Each input port has a buffer of its own; InputBlock wants them one
    // after another. Sized at open for the largest block PipeWire runs by
    // default, so the process callback never allocates.
    static constexpr std::size_t jack_input_capacity = 8192;
    std::vector<float> jack_input;
    bool jack_running = false;

    static int jack_process(jack_nframes_t frames, void* user) noexcept {
        auto* self = static_cast<Impl*>(user);
        auto* left = static_cast<float*>(jack_port_get_buffer(self->jack_outputs[0], frames));
        auto* right = static_cast<float*>(jack_port_get_buffer(self->jack_outputs[1], frames));
        InputBlock block;
        const auto channels = self->jack_inputs.size();
        if (channels > 0 && frames <= jack_input_capacity) {
            for (std::size_t channel = 0; channel < channels; ++channel)
                std::memcpy(self->jack_input.data() + channel * frames,
                            jack_port_get_buffer(self->jack_inputs[channel], frames),
                            frames * sizeof(float));
            block = {self->jack_input.data(), static_cast<std::uint32_t>(channels), frames,
                     frames};
        }
        self->render({std::span{left, frames}, std::span{right, frames}}, block);
        return 0;
    }

    bool open_jack(std::string* error);
    bool start_jack(std::string* error);
    void close_jack() noexcept;
#endif

    void render(StereoBlock output, InputBlock block) noexcept {
        const auto frames = static_cast<unsigned int>(output.left.size());
        callbacks.fetch_add(1, std::memory_order_relaxed);
        unsigned int seen = largest.load(std::memory_order_relaxed);
        while (frames > seen &&
               !largest.compare_exchange_weak(seen, frames, std::memory_order_relaxed)) {
        }
        const auto began = std::chrono::steady_clock::now();
        source->process(output, block);
        const auto rate = info.sample_rate;
        if (rate == 0 || frames == 0) return;
        const std::chrono::duration<double> took = std::chrono::steady_clock::now() - began;
        const auto load = static_cast<float>(took.count() * rate / frames);
        float worst = peak_load.load(std::memory_order_relaxed);
        while (load > worst &&
               !peak_load.compare_exchange_weak(worst, load, std::memory_order_relaxed)) {
        }
    }

    static int callback(void* output, void* input, unsigned int frames, double,
                        RtAudioStreamStatus status, void* user) {
        auto* self = static_cast<Impl*>(user);
        if (status != 0) self->dropouts.fetch_add(1, std::memory_order_relaxed);
        auto* samples = static_cast<float*>(output);
        // RtAudio hands a duplex stream's input non-interleaved, channel by
        // channel, exactly as InputBlock reads it.
        InputBlock block;
        if (input != nullptr && self->input_channels > 0)
            block = {static_cast<const float*>(input), self->input_channels, frames, frames};
        self->render({std::span{samples, frames}, std::span{samples + frames, frames}}, block);
        return 0;
    }
};

#if BLOKKILY_HAVE_JACK
namespace {
// The server's physical ports of one direction, in its order; empty when it
// has none.
std::vector<std::string> physical_ports(jack_client_t* client, unsigned long direction) {
    std::vector<std::string> names;
    const char** ports = jack_get_ports(client, nullptr, JACK_DEFAULT_AUDIO_TYPE,
                                        JackPortIsPhysical | direction);
    if (ports == nullptr) return names;
    for (auto** port = ports; *port != nullptr; ++port) names.emplace_back(*port);
    jack_free(static_cast<void*>(ports));
    return names;
}

// A port is "client:port"; the client is the device a patchbay shows.
std::string device_of(const std::string& port) { return port.substr(0, port.find(':')); }

std::uint32_t latency_of(jack_client_t* client, const std::string& port,
                         jack_latency_callback_mode_t mode) {
    jack_port_t* handle = jack_port_by_name(client, port.c_str());
    if (handle == nullptr) return 0;
    jack_latency_range_t range{};
    jack_port_get_latency_range(handle, mode, &range);
    return range.max;
}
} // namespace

bool RtAudioOutput::Impl::open_jack(std::string* error) {
    jack_status_t status{};
    // Never starts a server of its own: the one the desktop runs is the one
    // to play through.
    jack = jack_client_open("Blokkily", JackNoStartServer, &status);
    if (jack == nullptr) {
        if (error) *error = "no JACK server is running";
        return false;
    }
    jack_outputs[0] = jack_port_register(jack, "out_1", JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsOutput, 0);
    jack_outputs[1] = jack_port_register(jack, "out_2", JACK_DEFAULT_AUDIO_TYPE,
                                         JackPortIsOutput, 0);
    const auto playback = physical_ports(jack, JackPortIsInput);
    if (jack_outputs[0] == nullptr || jack_outputs[1] == nullptr || playback.empty()) {
        if (error) *error = playback.empty() ? "the JACK server has no output to play through"
                                             : "JACK refused the output ports";
        close_jack();
        return false;
    }
    opened_wanting_input = want_input;
    const auto capture = want_input ? physical_ports(jack, JackPortIsOutput)
                                    : std::vector<std::string>{};
    // A pair is what a track records at most; a few more let a track pick
    // its pair on an interface with several.
    for (std::size_t index = 0; index < std::min<std::size_t>(capture.size(), 8); ++index) {
        const auto name = "in_" + std::to_string(index + 1);
        jack_port_t* port = jack_port_register(jack, name.c_str(), JACK_DEFAULT_AUDIO_TYPE,
                                               JackPortIsInput, 0);
        if (port == nullptr) break;
        jack_inputs.push_back(port);
    }
    jack_input.assign(jack_inputs.size() * jack_input_capacity, 0.0F);
    input_channels = static_cast<unsigned int>(jack_inputs.size());
    (void)jack_set_xrun_callback(
        jack,
        [](void* user) noexcept -> int {
            static_cast<Impl*>(user)->dropouts.fetch_add(1, std::memory_order_relaxed);
            return 0;
        },
        this);
    if (jack_set_process_callback(jack, &Impl::jack_process, this) != 0) {
        if (error) *error = "JACK refused the process callback";
        close_jack();
        return false;
    }
    round_trip = latency_of(jack, playback.front(), JackPlaybackLatency) +
                 (input_channels > 0 ? latency_of(jack, capture.front(), JackCaptureLatency) : 0);
    info.api = RtAudio::getApiDisplayName(RtAudio::UNIX_JACK);
    info.device = device_of(playback.front());
    info.input_device = input_channels > 0 ? device_of(capture.front()) : std::string{};
    info.input_channels = input_channels;
    info.round_trip_frames = round_trip;
    info.sample_rate = jack_get_sample_rate(jack);
    // What the server says now; it may run another size once the client is
    // active, and every block is rendered at whatever size it arrives.
    info.buffer_frames = jack_get_buffer_size(jack);
    return true;
}

bool RtAudioOutput::Impl::start_jack(std::string* error) {
    if (jack == nullptr) {
        if (error) *error = "the JACK client is not open";
        return false;
    }
    if (jack_running) return true;
    if (jack_activate(jack) != 0) {
        if (error) *error = "JACK would not activate the client";
        return false;
    }
    jack_running = true;
    // Connected as RtAudio did: to the first physical outputs, and from the
    // first physical inputs. A single physical output takes both sides.
    const auto playback = physical_ports(jack, JackPortIsInput);
    for (std::size_t side = 0; side < 2 && !playback.empty(); ++side)
        (void)jack_connect(jack, jack_port_name(jack_outputs[side]),
                           playback[std::min(side, playback.size() - 1)].c_str());
    const auto capture = physical_ports(jack, JackPortIsOutput);
    for (std::size_t index = 0; index < jack_inputs.size() && index < capture.size(); ++index)
        (void)jack_connect(jack, capture[index].c_str(), jack_port_name(jack_inputs[index]));
    return true;
}

void RtAudioOutput::Impl::close_jack() noexcept {
    if (jack == nullptr) return;
    if (jack_running) (void)jack_deactivate(jack);
    jack_running = false;
    (void)jack_client_close(jack);
    jack = nullptr;
    jack_outputs = {};
    jack_inputs.clear();
    jack_input.clear();
}
#endif

RtAudioOutput::DeviceInfo RtAudioOutput::device_info() const { return impl_->info; }

std::uint64_t RtAudioOutput::callback_count() const noexcept {
    return impl_->callbacks.load(std::memory_order_relaxed);
}

unsigned int RtAudioOutput::largest_block() const noexcept {
    return impl_->largest.load(std::memory_order_relaxed);
}

float RtAudioOutput::take_peak_load() noexcept {
    return impl_->peak_load.exchange(0.0F, std::memory_order_relaxed);
}

std::uint64_t RtAudioOutput::dropouts() const noexcept {
    return impl_->dropouts.load(std::memory_order_relaxed);
}

RtAudioOutput::RtAudioOutput(Mode mode, unsigned int negotiated_rate)
    : impl_(std::make_unique<Impl>()) {
    impl_->deterministic = mode == Mode::deterministic;
    impl_->negotiated_rate = negotiated_rate;
    if (!impl_->deterministic) impl_->audio = std::make_unique<RtAudio>();
}
RtAudioOutput::RtAudioOutput(Mode mode, const std::string& api) : impl_(std::make_unique<Impl>()) {
    impl_->deterministic = mode == Mode::deterministic;
    if (impl_->deterministic) return;
#if BLOKKILY_HAVE_JACK
    if (api == RtAudio::getApiName(RtAudio::UNIX_JACK)) {
        impl_->use_jack = true;
        return;
    }
#endif
    if (api.empty()) {
        impl_->audio = std::make_unique<RtAudio>();
        return;
    }
    const auto chosen = RtAudio::getCompiledApiByName(api);
    if (chosen == RtAudio::UNSPECIFIED) {
        impl_->missing_api = api;
        return;
    }
    impl_->audio = std::make_unique<RtAudio>(chosen);
}

RtAudioOutput::Mode RtAudioOutput::mode() const noexcept {
    return impl_->deterministic ? Mode::deterministic : Mode::device;
}

std::vector<RtAudioOutput::AudioSystem> RtAudioOutput::audio_systems() {
    std::vector<RtAudio::Api> apis;
    RtAudio::getCompiledApi(apis);
    std::vector<AudioSystem> systems;
    for (const auto api : apis) {
        if (api == RtAudio::RTAUDIO_DUMMY) continue;
        systems.push_back({RtAudio::getApiName(api), RtAudio::getApiDisplayName(api)});
    }
    return systems;
}

namespace {
// RtAudio reports a device it cannot probe on stderr; a list being read is
// not an error to print.
void quiet(RtAudioErrorType, const std::string&) {}

// The device called `name` on `audio`, or `fallback` when none is.
unsigned int device_named(RtAudio& audio, const std::string& name, unsigned int fallback,
                          bool output) {
    if (name.empty()) return fallback;
    for (const auto id : audio.getDeviceIds()) {
        const auto info = audio.getDeviceInfo(id);
        if (info.name == name && (output ? info.outputChannels : info.inputChannels) > 0)
            return id;
    }
    return fallback;
}
} // namespace

std::vector<RtAudioOutput::AudioDevice> RtAudioOutput::devices(const std::string& api) {
    std::vector<AudioDevice> found;
#if BLOKKILY_HAVE_JACK
    if (api == RtAudio::getApiName(RtAudio::UNIX_JACK)) return found;
#endif
    const auto chosen = api.empty() ? RtAudio::UNSPECIFIED : RtAudio::getCompiledApiByName(api);
    if (!api.empty() && chosen == RtAudio::UNSPECIFIED) return found;
    RtAudio audio(chosen, &quiet);
    for (const auto id : audio.getDeviceIds()) {
        const auto info = audio.getDeviceInfo(id);
        if (info.name.empty() || (info.outputChannels == 0 && info.inputChannels == 0)) continue;
        found.push_back({info.name, info.outputChannels, info.inputChannels, info.sampleRates,
                         info.preferredSampleRate, info.isDefaultOutput, info.isDefaultInput});
    }
    return found;
}

void RtAudioOutput::set_devices(std::string output, std::string input) {
    impl_->output_name = std::move(output);
    impl_->input_name = std::move(input);
}

RtAudioOutput::~RtAudioOutput() {
    stop();
#if BLOKKILY_HAVE_JACK
    impl_->close_jack();
#endif
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
#if BLOKKILY_HAVE_JACK
    if (impl_->use_jack) {
        // The server sets the rate and the block: neither is asked for.
        (void)sample_rate;
        (void)requested_frames;
        impl_->close_jack();
        impl_->source = &source;
        return impl_->open_jack(error);
    }
#endif
    if (!impl_->audio) {
        if (error) *error = "audio system '" + impl_->missing_api + "' is not available in this build";
        return false;
    }
    if (impl_->audio->getDeviceCount() == 0) {
        if (error) *error = "no audio output device found";
        return false;
    }
    RtAudio::StreamParameters parameters;
    parameters.deviceId = device_named(*impl_->audio, impl_->output_name,
                                       impl_->audio->getDefaultOutputDevice(), true);
    parameters.nChannels = 2;
    parameters.firstChannel = 0;
    RtAudio::StreamOptions options;
    options.flags = RTAUDIO_NONINTERLEAVED | RTAUDIO_MINIMIZE_LATENCY;
    // What JACK (and PipeWire's JACK) list the client as in a patchbay.
    options.streamName = "Blokkily";
    impl_->source = &source;
    impl_->input_channels = 0;
    impl_->info.input_device.clear();
    // Duplex first, so a track can record what is plugged into the inputs
    // (item 3.2). A server that refuses it - a capture device at another rate,
    // one that is busy - still gets an output-only stream: playing never
    // depends on recording being possible.
    unsigned int frames = requested_frames;
    auto result = RTAUDIO_INVALID_USE;
    const auto input_device =
        impl_->want_input ? device_named(*impl_->audio, impl_->input_name,
                                         impl_->audio->getDefaultInputDevice(), false)
                          : 0U;
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
#if BLOKKILY_HAVE_JACK
    if (impl_->use_jack) return impl_->start_jack(error);
#endif
    if (!impl_->audio) {
        if (error) *error = "audio system '" + impl_->missing_api + "' is not available in this build";
        return false;
    }
    const auto result = impl_->audio->startStream();
    if (result == RTAUDIO_NO_ERROR) return true;
    if (error) *error = impl_->audio->getErrorText();
    return false;
}

void RtAudioOutput::stop() noexcept {
#if BLOKKILY_HAVE_JACK
    if (impl_->use_jack) {
        if (impl_->jack != nullptr && impl_->jack_running) (void)jack_deactivate(impl_->jack);
        impl_->jack_running = false;
        return;
    }
#endif
    if (impl_->deterministic) impl_->running = false;
    else if (impl_->audio && impl_->audio->isStreamRunning()) (void)impl_->audio->stopStream();
}
void RtAudioOutput::rebind(AudioSource& source) noexcept {
    if (impl_) impl_->source = &source;
}
bool RtAudioOutput::is_open() const noexcept {
#if BLOKKILY_HAVE_JACK
    if (impl_->use_jack) return impl_->jack != nullptr;
#endif
    return impl_->deterministic ? impl_->source != nullptr
                                : impl_->audio != nullptr && impl_->audio->isStreamOpen();
}
bool RtAudioOutput::is_running() const noexcept {
#if BLOKKILY_HAVE_JACK
    if (impl_->use_jack) return impl_->jack_running;
#endif
    return impl_->deterministic ? impl_->running
                                : impl_->audio != nullptr && impl_->audio->isStreamRunning();
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
#if BLOKKILY_HAVE_JACK
    impl_->close_jack();
#endif
    if (impl_->audio && impl_->audio->isStreamOpen()) impl_->audio->closeStream();
    impl_->input_channels = 0;
    impl_->round_trip = 0;
    impl_->info = {};
}

unsigned int RtAudioOutput::input_channels() const noexcept { return impl_->input_channels; }

std::uint32_t RtAudioOutput::round_trip_latency() const noexcept { return impl_->round_trip; }

} // namespace blokkily
