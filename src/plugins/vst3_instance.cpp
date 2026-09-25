#include "blokkily/plugins/vst3_instance.hpp"

#include "blokkily/audio/event_queue.hpp"
#include "blokkily/plugins/plugin_run_loop.hpp"

#include <juce_audio_processors/juce_audio_processors.h>

#include <dlfcn.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <mutex>
#include <system_error>
#include <type_traits>

// JUCE's own Linux message dispatch: runs whatever is ready on the message
// queue and on the descriptors JUCE watches (its X connection among them),
// without blocking when returnIfNoPendingMessages is true. Declared here
// because JUCE keeps it out of its public headers; it is what JUCE's own
// plugin wrappers call from the host's run loop.
namespace juce::detail {
bool dispatchNextMessageOnSystemQueue(bool returnIfNoPendingMessages);
}

namespace blokkily {

namespace {
constexpr const char* no_display_message = "Plugin window needs an X11 display";

// Xlib, reached through the same libX11.so.6 that JUCE opens with dlopen, so
// the error handler installed here is the one JUCE's connection uses. The
// types are spelled opaquely rather than taken from <X11/Xlib.h>, whose
// macros (None, Bool, Status...) collide with JUCE's names.
struct Xlib {
    using Display = void;
    using ErrorHandler = int (*)(Display*, void*);
    void* library = nullptr;
    Display* (*open_display)(const char*) = nullptr;
    int (*close_display)(Display*) = nullptr;
    ErrorHandler (*set_error_handler)(ErrorHandler) = nullptr;
    int (*get_geometry)(Display*, unsigned long, unsigned long*, int*, int*, unsigned*,
                        unsigned*, unsigned*, unsigned*) = nullptr;
    int (*sync)(Display*, int) = nullptr;

    static Xlib& get() {
        static Xlib xlib = [] {
            Xlib loaded;
            loaded.library = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
            if (loaded.library == nullptr) return loaded;
            const auto bind = [&](auto& function, const char* name) {
                function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
                    dlsym(loaded.library, name));
                return function != nullptr;
            };
            const bool complete = bind(loaded.open_display, "XOpenDisplay") &&
                                  bind(loaded.close_display, "XCloseDisplay") &&
                                  bind(loaded.set_error_handler, "XSetErrorHandler") &&
                                  bind(loaded.get_geometry, "XGetGeometry") &&
                                  bind(loaded.sync, "XSync");
            if (!complete) loaded.library = nullptr;
            return loaded;
        }();
        return xlib;
    }
};

std::atomic<int> x_errors{0};
// Counts an X error instead of letting Xlib's default handler exit the
// process: JUCE installs its own handlers only in a standalone JUCE
// application, which this host never is, and a stale parent or a plugin that
// misbehaves must not take the song down with it.
int count_x_error(Xlib::Display*, void*) {
    x_errors.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

// Whether a JUCE editor can be put on screen: a DISPLAY is named, the host can
// open it, and the window the editor would be embedded in is a real window on
// it. Refusing is the only safe answer otherwise: JUCE turns an empty DISPLAY
// into ":0.0" (which can hang for tens of seconds on another user's server),
// an editor peer without X segfaults, and embedding into a window that is not
// an X window (an offscreen or Wayland winId) makes Xlib exit the process.
bool x11_reachable(const NativeParent* parent) {
    const char* name = std::getenv("DISPLAY");
    if (name == nullptr || *name == '\0') return false;
    if (parent != nullptr && parent->api != WindowApi::x11) return false;
    auto& x = Xlib::get();
    if (x.library == nullptr) return false;
    static const bool handler_installed = [&x] {
        (void)x.set_error_handler(&count_x_error);
        return true;
    }();
    (void)handler_installed;
    auto* display = x.open_display(name);
    if (display == nullptr) return false;
    bool reachable = true;
    if (parent != nullptr) {
        const int errors_before = x_errors.load(std::memory_order_relaxed);
        unsigned long root = 0;
        int left = 0, top = 0;
        unsigned width = 0, height = 0, border = 0, depth = 0;
        reachable = x.get_geometry(display, static_cast<unsigned long>(parent->handle), &root,
                                   &left, &top, &width, &height, &border, &depth) != 0;
        (void)x.sync(display, 0);
        reachable = reachable && x_errors.load(std::memory_order_relaxed) == errors_before;
    }
    (void)x.close_display(display);
    return reachable;
}

// Pumps the host's JUCE message queue from the installed run loop while any
// VST3 instance lives: hosted plugins post messages to it, and a queue nobody
// empties overflows. An editor's painting and its X events arrive the same
// way. Main thread only, like every run-loop callback.
class JucePump {
public:
    static void acquire() {
        auto& pump = instance();
        if (pump.users_++ > 0) return;
        pump.loop_ = plugin_run_loop();
        if (pump.loop_ != nullptr) pump.timer_ = pump.loop_->add_timer(10, &JucePump::dispatch);
    }
    static void release() {
        auto& pump = instance();
        if (pump.users_ == 0 || --pump.users_ > 0) return;
        if (pump.loop_ != nullptr && pump.timer_ != 0) (void)pump.loop_->remove_timer(pump.timer_);
        pump.loop_ = nullptr;
        pump.timer_ = 0;
    }
    // Everything that is ready, within a bound, so a flood of messages cannot
    // hold the host's own event loop.
    static void dispatch() {
        for (int message = 0; message < 256; ++message)
            if (!juce::detail::dispatchNextMessageOnSystemQueue(true)) break;
    }

private:
    static JucePump& instance() {
        static JucePump pump;
        return pump;
    }
    int users_ = 0;
    PluginRunLoop* loop_ = nullptr;
    std::uint64_t timer_ = 0;
};

// A floating editor's own top-level window. Its close button asks the
// adapter to close the editor, which idle() then does: deleting the window
// from inside its own callback would pull it out from under JUCE.
class FloatingEditorWindow final : public juce::DocumentWindow {
public:
    FloatingEditorWindow(const juce::String& title, std::function<void()> on_close)
        : DocumentWindow(title, juce::Colours::black, DocumentWindow::closeButton, true),
          on_close_(std::move(on_close)) {
        setUsingNativeTitleBar(true);
    }
    void closeButtonPressed() override { on_close_(); }

private:
    std::function<void()> on_close_;
};
} // namespace

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

    // The editor (item 2.6), when one is open: embedded in the host's window,
    // or inside a floating window of its own. Main thread only.
    struct ResizeWatcher final : juce::ComponentListener {
        EditorHost* host = nullptr;
        void componentMovedOrResized(juce::Component& component, bool, bool resized) override {
            if (resized && host != nullptr)
                host->request_resize(static_cast<std::uint32_t>(component.getWidth()),
                                     static_cast<std::uint32_t>(component.getHeight()));
        }
    };
    std::unique_ptr<juce::AudioProcessorEditor> editor;
    std::unique_ptr<FloatingEditorWindow> floating;
    ResizeWatcher resize_watcher;
    EditorHost* editor_host = nullptr;
    bool close_requested = false;

    Impl() { JucePump::acquire(); }
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    ~Impl() override {
        // The editor goes before the plugin it edits, and the window that
        // hosted it is told, so it can go too.
        if (editor) {
            auto* host = editor_host;
            destroy_editor();
            if (host != nullptr) host->closed();
        }
        stop_listening();
        JucePump::release();
    }

    void destroy_editor() {
        if (editor) editor->removeComponentListener(&resize_watcher);
        floating.reset();
        editor.reset();
        editor_host = nullptr;
        resize_watcher.host = nullptr;
        close_requested = false;
    }

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
    juce::VST3PluginFormat format;
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
    juce::VST3PluginFormat format;
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
    const auto search = juce::VST3PluginFormat{}.getDefaultLocationsToSearch();
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
    juce::VST3PluginFormat format;
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
    if (!impl_ || !impl_->plugin) return;
    impl_->read_tail();
    // A floating editor's close button was pressed.
    if (impl_->close_requested && impl_->editor) {
        auto* host = impl_->editor_host;
        impl_->destroy_editor();
        if (host != nullptr) host->closed();
    }
}

bool Vst3PluginInstance::has_editor() const {
    return impl_ && impl_->plugin && impl_->plugin->hasEditor();
}

bool Vst3PluginInstance::supports_editor(WindowApi api, bool) const {
    // JUCE puts its editors on X11, embedded or as a top-level of their own.
    return api == WindowApi::x11 && has_editor();
}

bool Vst3PluginInstance::open_editor(const NativeParent* parent, EditorHost& host,
                                     EditorSize* size, std::string* error) {
    auto fail = [error](const char* message) {
        if (error != nullptr) *error = message;
        return false;
    };
    if (!impl_ || !impl_->plugin) return fail("No plugin");
    if (impl_->editor) return fail("The editor is already open");
    if (!impl_->plugin->hasEditor()) return fail("This plugin has no editor");
    // Decided before JUCE is asked for anything that would touch X.
    if (!x11_reachable(parent)) return fail(no_display_message);
    impl_->editor.reset(impl_->plugin->createEditorAndMakeActive());
    if (!impl_->editor) return fail("The plugin could not create its editor");
    auto& editor = *impl_->editor;
    if (parent != nullptr) {
        editor.addToDesktop(0, reinterpret_cast<void*>(parent->handle));
        editor.setVisible(true);
    } else {
        auto* self = impl_.get();
        impl_->floating = std::make_unique<FloatingEditorWindow>(
            impl_->plugin->getName(), [self] { self->close_requested = true; });
        impl_->floating->setContentNonOwned(&editor, true);
        impl_->floating->setVisible(true);
    }
    impl_->editor_host = &host;
    impl_->resize_watcher.host = &host;
    editor.addComponentListener(&impl_->resize_watcher);
    if (size != nullptr) {
        // JUCE sizes a Linux editor in physical pixels: it is not scaled by
        // the host window's device pixel ratio.
        size->width = static_cast<std::uint32_t>(std::max(0, editor.getWidth()));
        size->height = static_cast<std::uint32_t>(std::max(0, editor.getHeight()));
        size->resizable = editor.isResizable();
    }
    return true;
}

bool Vst3PluginInstance::resize_editor(std::uint32_t& width, std::uint32_t& height) {
    if (!impl_ || !impl_->editor || !impl_->editor->isResizable()) return false;
    impl_->editor->setSize(static_cast<int>(width), static_cast<int>(height));
    width = static_cast<std::uint32_t>(std::max(0, impl_->editor->getWidth()));
    height = static_cast<std::uint32_t>(std::max(0, impl_->editor->getHeight()));
    return true;
}

void Vst3PluginInstance::close_editor() {
    if (impl_) impl_->destroy_editor();
}

bool Vst3PluginInstance::editor_open() const { return impl_ && impl_->editor != nullptr; }

} // namespace blokkily
