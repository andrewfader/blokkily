#include "blokkily/plugins/vst3_instance.hpp"

#include "blokkily/audio/event_queue.hpp"

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <mutex>
#include <system_error>

namespace blokkily {

namespace {
// JUCE recognises a VST3 bundle by the literal spelling of the path, so an
// unnormalised path such as `.../Contents/x86_64-linux/../..` scans as nothing.
juce::String bundle_string(const std::filesystem::path& path) {
    std::error_code failure;
    const auto resolved = std::filesystem::weakly_canonical(path, failure);
    return juce::String((failure ? path.lexically_normal() : resolved).string());
}
}

namespace {
// True on a thread while it is inside this adapter's process(). A parameter
// listener that fires there (the plugin reporting an output parameter) must
// not wait for the edit ring's producer lock.
thread_local bool inside_vst3_process = false;

struct ProcessScope {
    ProcessScope() noexcept { inside_vst3_process = true; }
    ~ProcessScope() { inside_vst3_process = false; }
    ProcessScope(const ProcessScope&) = delete;
    ProcessScope& operator=(const ProcessScope&) = delete;
};

// The transport handed to the plugin, read by JUCE on the audio thread while
// it builds the VST3 ProcessContext for the block.
class TransportPlayHead final : public juce::AudioPlayHead {
public:
    void set(const TransportInfo& info, double sample_rate) noexcept {
        info_ = info;
        // VST3 requires a sample position. The transport speaks in beats, so
        // the position is the beat at the current tempo: exact for a steady
        // tempo, and an estimate across tempo changes, where plugins should
        // follow the beat position instead.
        seconds_ = info.bpm > 0.0 ? info.beat * 60.0 / info.bpm : 0.0;
        samples_ = static_cast<std::int64_t>(std::llround(seconds_ * sample_rate));
        known_ = true;
    }
    juce::Optional<PositionInfo> getPosition() const override {
        if (!known_) return {};
        PositionInfo position;
        position.setTimeInSamples(samples_);
        position.setTimeInSeconds(seconds_);
        position.setBpm(info_.bpm);
        position.setPpqPosition(info_.beat);
        position.setBarCount(info_.bar);
        position.setTimeSignature(
            juce::AudioPlayHead::TimeSignature{info_.numerator, info_.denominator});
        position.setIsPlaying(info_.playing);
        return position;
    }

private:
    TransportInfo info_{};
    double seconds_ = 0.0;
    std::int64_t samples_ = 0;
    bool known_ = false;
};
} // namespace

// JUCE hosting requires an initialised message manager: plugin formats create
// message listeners and async updaters while scanning and instantiating. The
// initialiser is reference counted, and JUCE must outlive the hosted plugin, so
// every adapter object owns one that is declared before what it protects.
struct Vst3PluginInstance::Impl final : private juce::AudioProcessorParameter::Listener {
    juce::ScopedJuceInitialiser_GUI juce_lifetime;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    int maximum_block_size = 0;
    double sample_rate = 48000.0;
    // Automation and modulation are kept apart per parameter: the plugin sees
    // their sum, but a later automation event never erases the modulation and
    // a later modulation never overwrites the automated value. The automation
    // base is atomic because the plugin moves it too, from its own threads:
    // a knob turned in its window, or a loaded state, becomes the base that
    // modulation is added to.
    std::size_t parameter_count = 0;
    std::unique_ptr<std::atomic<float>[]> automation;
    std::unique_ptr<float[]> modulation; // audio thread only
    juce::MidiBuffer midi; // reused so processing never allocates
    // A retuned note is sent on a channel of its own and bent into place, the
    // way MPE hosts do it, because pitch bend belongs to a channel. Notes in
    // twelve-tone tuning stay on channel one, exactly as before.
    std::array<int, 16> channel_key{};
    bool announce_bend_range = true;

    // Edits the plugin made to its own parameters, reported by the listeners.
    std::mutex edits_producer;
    SpscQueue<ParameterEdit, 512> edits;

    std::atomic<std::uint32_t> latency{0};
    std::atomic<std::uint64_t> tail{0};
    std::uint32_t input_channels = 0;
    TransportPlayHead play_head;

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl() override { stop_listening(); }

    void attach(std::unique_ptr<juce::AudioPluginInstance> instance) {
        plugin = std::move(instance);
        enable_stereo_input();
        const auto& parameters = plugin->getParameters();
        parameter_count = static_cast<std::size_t>(parameters.size());
        automation = std::make_unique<std::atomic<float>[]>(parameter_count);
        modulation = std::make_unique<float[]>(parameter_count);
        seed_automation();
        // Only automatable parameters are the plugin's own: JUCE-built plugins
        // also expose thousands of hidden MIDI CC parameters, which move when
        // the host sends controllers and are not edits anyone made.
        for (auto* parameter : parameters)
            if (parameter->isAutomatable()) parameter->addListener(this);
        plugin->setPlayHead(&play_head);
    }

    void stop_listening() {
        if (!plugin) return;
        for (auto* parameter : plugin->getParameters())
            if (parameter->isAutomatable()) parameter->removeListener(this);
        plugin->setPlayHead(nullptr);
    }

    // An effect hears the track on its main input: a stereo bus when the
    // plugin can take one, and no side chains, so the two channels of the
    // block are all the input there is.
    void enable_stereo_input() {
        input_channels = 0;
        const auto layout = plugin->getBusesLayout();
        if (layout.inputBuses.isEmpty()) return;
        auto wanted = layout;
        wanted.inputBuses.getReference(0) = juce::AudioChannelSet::stereo();
        for (int bus = 1; bus < wanted.inputBuses.size(); ++bus)
            wanted.inputBuses.getReference(bus) = juce::AudioChannelSet::disabled();
        if (plugin->checkBusesLayoutSupported(wanted)) (void)plugin->setBusesLayout(wanted);
        input_channels = static_cast<std::uint32_t>(
            std::clamp(plugin->getMainBusNumInputChannels(), 0, 2));
    }

    // The automation base becomes what the plugin holds now.
    void seed_automation() noexcept {
        const auto& parameters = plugin->getParameters();
        for (std::size_t index = 0; index < parameter_count; ++index)
            automation[index].store(parameters[static_cast<int>(index)]->getValue(),
                                    std::memory_order_relaxed);
    }

    void read_tail() {
        const double seconds = plugin->getTailLengthSeconds();
        std::uint64_t samples = 0;
        if (std::isinf(seconds)) samples = std::numeric_limits<std::uint64_t>::max();
        else if (seconds > 0.0) samples = static_cast<std::uint64_t>(std::llround(seconds * sample_rate));
        tail.store(samples, std::memory_order_release);
    }

    void apply(int index) const noexcept {
        const auto& parameters = plugin->getParameters();
        if (index < 0 || static_cast<std::size_t>(index) >= parameter_count) return;
        const auto slot = static_cast<std::size_t>(index);
        parameters[index]->setValue(std::clamp(
            automation[slot].load(std::memory_order_relaxed) + modulation[slot], 0.0F, 1.0F));
    }

    void queue(const ParameterEdit& edit) noexcept {
        // On the audio thread the producer lock is only tried: an edit is
        // dropped rather than the render callback made to wait.
        if (inside_vst3_process) {
            if (!edits_producer.try_lock()) return;
        } else {
            edits_producer.lock();
        }
        (void)edits.push(edit);
        edits_producer.unlock();
    }

    // Called by JUCE when the plugin itself moves a parameter: its editor
    // through the component handler, or an output parameter from process().
    // The host's own setValue() in apply() does not notify listeners, so
    // automation and modulation are never echoed back as edits.
    void parameterValueChanged(int index, float value) override {
        if (index < 0 || static_cast<std::size_t>(index) >= parameter_count) return;
        automation[static_cast<std::size_t>(index)].store(value, std::memory_order_relaxed);
        queue({ParameterEdit::Kind::value, index, static_cast<double>(value), 0});
    }
    void parameterGestureChanged(int index, bool starting) override {
        queue({starting ? ParameterEdit::Kind::begin : ParameterEdit::Kind::end, index, 0.0, 0});
    }
};

namespace {
juce::OwnedArray<juce::PluginDescription> descriptions(const std::filesystem::path& path) {
    juce::VST3PluginFormatHeadless format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile(found, bundle_string(path));
    return found;
}
}

std::vector<Vst3Descriptor> Vst3PluginInstance::scan(const std::filesystem::path& bundle) {
    const juce::ScopedJuceInitialiser_GUI juce_lifetime;
    std::vector<Vst3Descriptor> result;
    std::size_t index = 0;
    for (const auto* description : descriptions(bundle)) {
        result.push_back({description->name.toStdString(), description->manufacturerName.toStdString(),
                          description->createIdentifierString().toStdString(), bundle, index});
        ++index;
    }
    return result;
}

std::vector<Vst3Descriptor> Vst3PluginInstance::scan_paths(
    const std::vector<std::filesystem::path>& roots) {
    const juce::ScopedJuceInitialiser_GUI juce_lifetime;
    juce::VST3PluginFormatHeadless format;
    juce::FileSearchPath search;
    std::vector<Vst3Descriptor> result;
    for (const auto& root : roots) {
        std::error_code error;
        const auto canonical = std::filesystem::weakly_canonical(root, error);
        const auto normalized = error ? root.lexically_normal() : canonical;
        if (normalized.extension() == ".vst3") {
            for (auto& descriptor : scan(normalized)) result.push_back(std::move(descriptor));
        } else {
            search.addIfNotAlreadyThere(juce::File(bundle_string(normalized)));
        }
    }
    for (const auto& bundle : format.searchPathsForPlugins(search, true, false))
        for (auto& descriptor : scan(bundle.toStdString()))
            result.push_back(std::move(descriptor));
    return result;
}

std::vector<std::filesystem::path> Vst3PluginInstance::system_paths() {
    const juce::ScopedJuceInitialiser_GUI juce_lifetime;
    const auto search = juce::VST3PluginFormatHeadless{}.getDefaultLocationsToSearch();
    std::vector<std::filesystem::path> result;
    for (int index = 0; index < search.getNumPaths(); ++index)
        result.emplace_back(search[index].getFullPathName().toStdString());
    return result;
}

std::unique_ptr<Vst3PluginInstance> Vst3PluginInstance::create(
    const std::filesystem::path& bundle, std::size_t index, std::string* error) {
    // Built first so that JUCE stays initialised across the scan and the
    // instantiation below rather than being torn down between them.
    auto implementation = std::make_unique<Impl>();
    auto found = descriptions(bundle);
    if (index >= static_cast<std::size_t>(found.size())) {
        if (error) *error = "VST3 descriptor not found";
        return nullptr;
    }
    juce::VST3PluginFormatHeadless format;
    juce::String message;
    auto plugin = format.createInstanceFromDescription(*found[static_cast<int>(index)], 48000.0, 512, message);
    if (!plugin) {
        if (error) *error = message.toStdString();
        return nullptr;
    }
    implementation->attach(std::move(plugin));
    return std::unique_ptr<Vst3PluginInstance>(new Vst3PluginInstance(std::move(implementation)));
}

Vst3PluginInstance::Vst3PluginInstance(std::unique_ptr<Impl> implementation)
    : impl_(std::move(implementation)) {}
Vst3PluginInstance::~Vst3PluginInstance() = default;
Vst3PluginInstance::Vst3PluginInstance(Vst3PluginInstance&&) noexcept = default;
Vst3PluginInstance& Vst3PluginInstance::operator=(Vst3PluginInstance&&) noexcept = default;

bool Vst3PluginInstance::activate(double sample_rate, std::uint32_t,
                                  std::uint32_t max_frames) {
    if (!impl_ || !impl_->plugin || max_frames == 0) return false;
    impl_->maximum_block_size = static_cast<int>(max_frames);
    impl_->sample_rate = sample_rate;
    impl_->plugin->setRateAndBufferSizeDetails(sample_rate, impl_->maximum_block_size);
    impl_->plugin->prepareToPlay(sample_rate, impl_->maximum_block_size);
    std::fill_n(impl_->modulation.get(), impl_->parameter_count, 0.0F);
    impl_->seed_automation();
    impl_->latency.store(static_cast<std::uint32_t>(std::max(0, impl_->plugin->getLatencySamples())),
                         std::memory_order_release);
    impl_->read_tail();
    // Events are bounded independently of audio frames. Even a one-sample
    // window can contain the entire host event budget plus bend-range setup.
    impl_->midi.ensureSize(std::max<std::size_t>(
        static_cast<std::size_t>(impl_->maximum_block_size) * 4, 16384));
    impl_->midi.clear();
    impl_->channel_key.fill(-1);
    impl_->announce_bend_range = true;
    return true;
}

void Vst3PluginInstance::process(StereoBlock audio,
                                 std::span<const PluginEvent> events) noexcept {
    if (!impl_ || !impl_->plugin || audio.left.size() != audio.right.size() ||
        audio.left.size() > static_cast<std::size_t>(impl_->maximum_block_size)) return;
    const auto frames = audio.left.size();
    if (frames == 0) return;
    const ProcessScope audio_thread;

    const auto offset_of = [frames](const PluginEvent& event) {
        return std::min<std::size_t>(event.sample_offset, frames - 1);
    };
    const auto is_parameter = [](const PluginEvent& event) {
        return event.type == PluginEvent::Type::parameter_value ||
               event.type == PluginEvent::Type::parameter_modulation;
    };

    // JUCE applies a parameter change to a whole block, so the block is split
    // at every parameter offset. That makes VST3 automation sample-accurate to
    // the same degree the CLAP path already is.
    std::size_t rendered = 0;
    while (rendered < frames) {
        for (const auto& event : events) {
            if (!is_parameter(event)) continue;
            const auto offset = offset_of(event);
            const bool due = rendered == 0 ? offset <= rendered : offset == rendered;
            if (!due) continue;
            const auto index = static_cast<std::size_t>(event.key_or_parameter);
            if (event.key_or_parameter < 0 || index >= impl_->parameter_count) continue;
            if (event.type == PluginEvent::Type::parameter_value)
                impl_->automation[index].store(static_cast<float>(event.value),
                                               std::memory_order_relaxed);
            else
                impl_->modulation[index] = static_cast<float>(event.value);
            impl_->apply(event.key_or_parameter);
        }

        std::size_t boundary = frames;
        for (const auto& event : events) {
            if (!is_parameter(event)) continue;
            const auto offset = offset_of(event);
            if (offset > rendered && offset < boundary) boundary = offset;
        }

        impl_->midi.clear();
        if (impl_->announce_bend_range) {
            // Every voice channel is told that its bend wheel spans two
            // semitones, so a retune offset means the same to the plugin as it
            // does here. Sent once, with the first audio the plugin sees.
            for (int channel = 2; channel <= 16; ++channel) {
                impl_->midi.addEvent(juce::MidiMessage::controllerEvent(channel, 101, 0), 0);
                impl_->midi.addEvent(juce::MidiMessage::controllerEvent(channel, 100, 0), 0);
                impl_->midi.addEvent(juce::MidiMessage::controllerEvent(channel, 6, 2), 0);
                impl_->midi.addEvent(juce::MidiMessage::controllerEvent(channel, 38, 0), 0);
            }
            impl_->announce_bend_range = false;
        }
        for (const auto& event : events) {
            if (is_parameter(event)) continue;
            const auto offset = offset_of(event);
            if (offset < rendered || offset >= boundary) continue;
            const auto position = static_cast<int>(offset - rendered);
            if (event.type == PluginEvent::Type::note_on) {
                int channel = 1;
                if (event.cents != 0.0) {
                    channel = 2;
                    for (int candidate = 2; candidate <= 16; ++candidate)
                        if (impl_->channel_key[static_cast<std::size_t>(candidate) - 1] < 0) {
                            channel = candidate;
                            break;
                        }
                    impl_->channel_key[static_cast<std::size_t>(channel) - 1] =
                        event.key_or_parameter;
                    const int bend = std::clamp(
                        8192 + static_cast<int>(std::lround(event.cents / 200.0 * 8192.0)),
                        0, 16383);
                    impl_->midi.addEvent(juce::MidiMessage::pitchWheel(channel, bend), position);
                }
                impl_->midi.addEvent(juce::MidiMessage::noteOn(channel, event.key_or_parameter,
                                     static_cast<float>(event.value)), position);
            } else if (event.type == PluginEvent::Type::note_off) {
                int channel = 1;
                for (int candidate = 2; candidate <= 16; ++candidate)
                    if (impl_->channel_key[static_cast<std::size_t>(candidate) - 1] ==
                        event.key_or_parameter) {
                        channel = candidate;
                        impl_->channel_key[static_cast<std::size_t>(candidate) - 1] = -1;
                        break;
                    }
                impl_->midi.addEvent(juce::MidiMessage::noteOff(channel, event.key_or_parameter,
                                     static_cast<float>(event.value)), position);
            }
        }

        float* channels[]{audio.left.data() + rendered, audio.right.data() + rendered};
        juce::AudioBuffer<float> buffer(channels, 2, static_cast<int>(boundary - rendered));
        impl_->plugin->processBlock(buffer, impl_->midi);
        rendered = boundary;
    }
}

std::vector<std::byte> Vst3PluginInstance::save_state() {
    juce::MemoryBlock block;
    if (!impl_ || !impl_->plugin) return {};
    impl_->plugin->getStateInformation(block);
    std::vector<std::byte> result(block.getSize());
    std::memcpy(result.data(), block.getData(), block.getSize());
    return result;
}

bool Vst3PluginInstance::load_state(std::span<const std::byte> state) {
    if (!impl_ || !impl_->plugin) return false;
    impl_->plugin->setStateInformation(state.data(), static_cast<int>(state.size()));
    // The state moved the plugin's parameters; the automation base follows,
    // or the next modulation would be added to the values from before.
    impl_->seed_automation();
    return true;
}

PluginPorts Vst3PluginInstance::ports() const {
    if (!impl_ || !impl_->plugin) return {};
    return {impl_->input_channels, impl_->plugin->acceptsMidi()};
}

std::uint32_t Vst3PluginInstance::latency_samples() const noexcept {
    return impl_ ? impl_->latency.load(std::memory_order_acquire) : 0;
}

std::uint64_t Vst3PluginInstance::tail_samples() const noexcept {
    return impl_ ? impl_->tail.load(std::memory_order_acquire) : 0;
}

bool Vst3PluginInstance::latency_changed() noexcept {
    if (!impl_ || !impl_->plugin) return false;
    const auto now = static_cast<std::uint32_t>(std::max(0, impl_->plugin->getLatencySamples()));
    return impl_->latency.exchange(now, std::memory_order_acq_rel) != now;
}

std::vector<ParameterInfo> Vst3PluginInstance::parameters() const {
    std::vector<ParameterInfo> result;
    if (!impl_ || !impl_->plugin) return result;
    const auto& parameters = impl_->plugin->getParameters();
    result.reserve(static_cast<std::size_t>(parameters.size()));
    for (int index = 0; index < parameters.size(); ++index) {
        const auto* parameter = parameters[index];
        // As for edits: the hidden MIDI CC parameters are not listed.
        if (!parameter->isAutomatable()) continue;
        result.push_back({index, parameter->getName(128).toStdString(), 0.0, 1.0,
                          static_cast<double>(parameter->getDefaultValue()),
                          parameter->isAutomatable()});
    }
    return result;
}

std::size_t Vst3PluginInstance::take_parameter_edits(std::span<ParameterEdit> out) noexcept {
    if (!impl_) return 0;
    std::size_t count = 0;
    while (count < out.size() && impl_->edits.pop(out[count])) ++count;
    return count;
}

void Vst3PluginInstance::set_transport(const TransportInfo& transport) noexcept {
    if (impl_) impl_->play_head.set(transport, impl_->sample_rate);
}

void Vst3PluginInstance::idle() {
    if (impl_ && impl_->plugin) impl_->read_tail();
}

} // namespace blokkily
