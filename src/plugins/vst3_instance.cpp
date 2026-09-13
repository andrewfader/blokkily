#include "blokkily/plugins/vst3_instance.hpp"

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
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

// JUCE hosting requires an initialised message manager: plugin formats create
// message listeners and async updaters while scanning and instantiating. The
// initialiser is reference counted, and JUCE must outlive the hosted plugin, so
// every adapter object owns one that is declared before what it protects.
struct Vst3PluginInstance::Impl {
    juce::ScopedJuceInitialiser_GUI juce_lifetime;
    std::unique_ptr<juce::AudioPluginInstance> plugin;
    int maximum_block_size = 0;
    // Automation and modulation are kept apart per parameter: the plugin sees
    // their sum, but a later automation event never erases the modulation and
    // a later modulation never overwrites the automated value.
    std::vector<float> automation;
    std::vector<float> modulation;
    juce::MidiBuffer midi; // reused so processing never allocates
    // A retuned note is sent on a channel of its own and bent into place, the
    // way MPE hosts do it, because pitch bend belongs to a channel. Notes in
    // twelve-tone tuning stay on channel one, exactly as before.
    std::array<int, 16> channel_key{};
    bool announce_bend_range = true;

    void apply(int index) const noexcept {
        const auto& parameters = plugin->getParameters();
        if (index < 0 || index >= parameters.size()) return;
        parameters[index]->setValue(
            std::clamp(automation[static_cast<std::size_t>(index)] +
                       modulation[static_cast<std::size_t>(index)], 0.0F, 1.0F));
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
    implementation->plugin = std::move(plugin);
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
    impl_->plugin->setRateAndBufferSizeDetails(sample_rate, impl_->maximum_block_size);
    impl_->plugin->prepareToPlay(sample_rate, impl_->maximum_block_size);
    const auto parameters = static_cast<std::size_t>(impl_->plugin->getParameters().size());
    impl_->automation.assign(parameters, 0.0F);
    impl_->modulation.assign(parameters, 0.0F);
    for (std::size_t index = 0; index < parameters; ++index)
        impl_->automation[index] = impl_->plugin->getParameters()[static_cast<int>(index)]->getValue();
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
            if (event.key_or_parameter < 0) continue;
            if (event.type == PluginEvent::Type::parameter_value) {
                if (index < impl_->automation.size())
                    impl_->automation[index] = static_cast<float>(event.value);
            } else if (index < impl_->modulation.size()) {
                impl_->modulation[index] = static_cast<float>(event.value);
            }
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
    return true;
}

} // namespace blokkily
