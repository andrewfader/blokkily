#include "blokkily/plugins/clap_instance.hpp"

#include "blokkily/audio/event_queue.hpp"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/latency.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>
#include <clap/ext/thread-check.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace blokkily {
namespace {

void* open_library(const std::filesystem::path& path) {
#if defined(_WIN32)
    return LoadLibraryW(path.c_str());
#else
    return dlopen(path.c_str(), RTLD_LOCAL | RTLD_NOW);
#endif
}
void close_library(void* library) {
#if defined(_WIN32)
    if (library) FreeLibrary(static_cast<HMODULE>(library));
#else
    if (library) dlclose(library);
#endif
}
const clap_plugin_entry_t* find_entry(void* library) {
#if defined(_WIN32)
    return reinterpret_cast<const clap_plugin_entry_t*>(
        GetProcAddress(static_cast<HMODULE>(library), "clap_entry"));
#else
    return reinterpret_cast<const clap_plugin_entry_t*>(dlsym(library, "clap_entry"));
#endif
}

// True on a thread while it is inside this host's process(): that is what
// "the audio thread" means to a plugin asking thread-check.
thread_local bool inside_audio_processing = false;

struct AudioThreadScope {
    AudioThreadScope() noexcept : previous(inside_audio_processing) {
        inside_audio_processing = true;
    }
    ~AudioThreadScope() { inside_audio_processing = previous; }
    AudioThreadScope(const AudioThreadScope&) = delete;
    AudioThreadScope& operator=(const AudioThreadScope&) = delete;
    bool previous;
};

// What the host keeps for one plugin so that it can answer the plugin's
// requests. `clap_host_t::host_data` points here. Every request may arrive on
// any thread, so each one only raises a flag that idle() or the next
// process() serves.
struct HostState {
    // The thread the instance was created on, which is the host's main thread.
    std::thread::id main_thread = std::this_thread::get_id();
    std::atomic<bool> callback_requested{false};
    std::atomic<bool> flush_requested{false};
    std::atomic<bool> latency_announced{false};
    std::atomic<bool> tail_announced{false};
    std::atomic<bool> parameters_rescanned{false};
    // Edits from out_events. The producer is whoever runs the plugin's
    // process() or flush(), which CLAP never runs at the same time; the
    // consumer is take_parameter_edits().
    SpscQueue<ParameterEdit, 512> edits;
};

HostState* state_of(const clap_host_t* host) { return static_cast<HostState*>(host->host_data); }

bool CLAP_ABI is_main_thread(const clap_host_t* host) {
    return std::this_thread::get_id() == state_of(host)->main_thread;
}
bool CLAP_ABI is_audio_thread(const clap_host_t*) { return inside_audio_processing; }
constexpr clap_host_thread_check_t host_thread_check{is_main_thread, is_audio_thread};

void CLAP_ABI params_rescan(const clap_host_t* host, clap_param_rescan_flags) {
    // parameters() asks the plugin afresh every time; the flag only records
    // that the list may have changed.
    state_of(host)->parameters_rescanned.store(true, std::memory_order_release);
}
void CLAP_ABI params_clear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void CLAP_ABI params_request_flush(const clap_host_t* host) {
    state_of(host)->flush_requested.store(true, std::memory_order_release);
}
constexpr clap_host_params_t host_params{params_rescan, params_clear, params_request_flush};

void CLAP_ABI latency_changed_callback(const clap_host_t* host) {
    state_of(host)->latency_announced.store(true, std::memory_order_release);
}
constexpr clap_host_latency_t host_latency{latency_changed_callback};

void CLAP_ABI tail_changed_callback(const clap_host_t* host) {
    state_of(host)->tail_announced.store(true, std::memory_order_release);
}
constexpr clap_host_tail_t host_tail{tail_changed_callback};

bool CLAP_ABI audio_ports_rescan_supported(const clap_host_t*, std::uint32_t) { return false; }
void CLAP_ABI audio_ports_rescan(const clap_host_t*, std::uint32_t) {}
constexpr clap_host_audio_ports_t host_audio_ports{audio_ports_rescan_supported,
                                                   audio_ports_rescan};

const void* CLAP_ABI host_extension(const clap_host_t*, const char* id) {
    if (id == nullptr) return nullptr;
    if (std::strcmp(id, CLAP_EXT_THREAD_CHECK) == 0) return &host_thread_check;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &host_params;
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &host_latency;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &host_tail;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &host_audio_ports;
    return nullptr;
}
// A restart is how a plugin asks to be deactivated and activated again,
// usually because its latency moved; the owner learns of it through
// latency_changed() and reactivates.
void CLAP_ABI host_restart(const clap_host_t* host) {
    state_of(host)->latency_announced.store(true, std::memory_order_release);
}
void CLAP_ABI host_process(const clap_host_t*) {}
void CLAP_ABI host_callback(const clap_host_t* host) {
    state_of(host)->callback_requested.store(true, std::memory_order_release);
}

union EventStorage {
    clap_event_note_t note;
    clap_event_note_expression_t expression;
    clap_event_param_value_t value;
    clap_event_param_mod_t modulation;
};

struct InputEvents {
    clap_input_events_t api;
    const EventStorage* events;
    std::uint32_t count;
};
std::uint32_t CLAP_ABI input_size(const clap_input_events_t* list) {
    return static_cast<const InputEvents*>(list->ctx)->count;
}
const clap_event_header_t* CLAP_ABI input_get(const clap_input_events_t* list,
                                               std::uint32_t index) {
    const auto* input = static_cast<const InputEvents*>(list->ctx);
    return index < input->count ? &input->events[index].note.header : nullptr;
}
std::uint32_t CLAP_ABI empty_size(const clap_input_events_t*) { return 0; }
const clap_event_header_t* CLAP_ABI empty_get(const clap_input_events_t*, std::uint32_t) {
    return nullptr;
}

// out_events: parameter gestures and values become ParameterEdits. Anything
// else the plugin sends (notes, MIDI) has no consumer yet and is accepted.
bool CLAP_ABI push_output(const clap_output_events_t* list, const clap_event_header_t* event) {
    auto* state = static_cast<HostState*>(list->ctx);
    if (event == nullptr || event->space_id != CLAP_CORE_EVENT_SPACE_ID) return true;
    ParameterEdit edit{};
    switch (event->type) {
    case CLAP_EVENT_PARAM_GESTURE_BEGIN:
    case CLAP_EVENT_PARAM_GESTURE_END: {
        const auto* gesture = reinterpret_cast<const clap_event_param_gesture_t*>(event);
        edit = {event->type == CLAP_EVENT_PARAM_GESTURE_BEGIN ? ParameterEdit::Kind::begin
                                                              : ParameterEdit::Kind::end,
                static_cast<std::int32_t>(gesture->param_id), 0.0, event->time};
        break;
    }
    case CLAP_EVENT_PARAM_VALUE: {
        const auto* value = reinterpret_cast<const clap_event_param_value_t*>(event);
        edit = {ParameterEdit::Kind::value, static_cast<std::int32_t>(value->param_id),
                value->value, event->time};
        break;
    }
    default:
        return true;
    }
    // A full ring drops the edit rather than making the audio thread wait.
    return state->edits.push(edit);
}

struct VectorOutput { clap_ostream_t stream; std::vector<std::byte>* bytes; };
std::int64_t CLAP_ABI write_bytes(const clap_ostream_t* stream, const void* buffer,
                                  std::uint64_t size) {
    auto* output = static_cast<VectorOutput*>(stream->ctx);
    const auto old_size = output->bytes->size();
    output->bytes->resize(old_size + static_cast<std::size_t>(size));
    std::memcpy(output->bytes->data() + old_size, buffer, static_cast<std::size_t>(size));
    return static_cast<std::int64_t>(size);
}
struct VectorInput { clap_istream_t stream; std::span<const std::byte> bytes; std::size_t cursor; };
std::int64_t CLAP_ABI read_bytes(const clap_istream_t* stream, void* buffer,
                                 std::uint64_t size) {
    auto* input = static_cast<VectorInput*>(stream->ctx);
    const auto count = std::min<std::size_t>(input->bytes.size() - input->cursor,
                                             static_cast<std::size_t>(size));
    if (count == 0) return 0;
    std::memcpy(buffer, input->bytes.data() + input->cursor, count);
    input->cursor += count;
    return static_cast<std::int64_t>(count);
}

} // namespace

struct ClapPluginInstance::Impl {
    void* library = nullptr;
    const clap_plugin_entry_t* entry = nullptr;
    const clap_plugin_t* plugin = nullptr;
    HostState state;
    clap_host_t host{CLAP_VERSION, &state, "Blokkily", "Blokkily", "https://blokkily.invalid",
                     "0.1.0", host_extension, host_restart, host_process, host_callback};
    clap_output_events_t output{&state, push_output};
    std::string plugin_id;
    std::string plugin_name;
    bool active = false;
    bool processing = false;
    std::int64_t steady_time = 0;

    std::atomic<std::uint32_t> latency{0};
    std::atomic<std::uint64_t> tail{0};

    // Audio ports as the plugin declared them. Without the extension the
    // original contract holds: a stereo output and no audio input.
    std::uint32_t input_channels = 0;
    std::uint32_t output_channels = 2;
    bool note_input = true;
    // The block arrives holding the plugin's input, and the plugin writes its
    // output over it, so the input is copied here first: CLAP expects separate
    // input and output buffers unless a port pairs them for in-place use.
    // Sized in activate(), never on the audio thread.
    std::vector<float> input_left;
    std::vector<float> input_right;

    clap_event_transport_t transport{};
    bool has_transport = false;

    template <typename Extension>
    const Extension* extension(const char* id) const {
        return plugin ? static_cast<const Extension*>(plugin->get_extension(plugin, id)) : nullptr;
    }

    void read_ports() {
        input_channels = 0;
        output_channels = 2;
        if (const auto* ports = extension<clap_plugin_audio_ports_t>(CLAP_EXT_AUDIO_PORTS)) {
            clap_audio_port_info_t info{};
            if (ports->count(plugin, true) > 0 && ports->get(plugin, 0, true, &info))
                input_channels = std::min<std::uint32_t>(info.channel_count, 2);
            output_channels = 0;
            if (ports->count(plugin, false) > 0 && ports->get(plugin, 0, false, &info))
                output_channels = std::min<std::uint32_t>(info.channel_count, 2);
        }
        const auto* notes = extension<clap_plugin_note_ports_t>(CLAP_EXT_NOTE_PORTS);
        note_input = notes == nullptr || notes->count(plugin, true) > 0;
    }

    // [main-thread & active]
    void read_latency() {
        const auto* plugin_latency = extension<clap_plugin_latency_t>(CLAP_EXT_LATENCY);
        latency.store(plugin_latency ? plugin_latency->get(plugin) : 0, std::memory_order_release);
    }

    void read_tail() {
        const auto* plugin_tail = extension<clap_plugin_tail_t>(CLAP_EXT_TAIL);
        const std::uint32_t samples = plugin_tail ? plugin_tail->get(plugin) : 0;
        // CLAP says INT32_MAX or more means the tail never ends.
        tail.store(samples >= static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
                       ? std::numeric_limits<std::uint64_t>::max()
                       : samples,
                   std::memory_order_release);
    }

    ~Impl() {
        if (plugin) {
            if (processing) plugin->stop_processing(plugin);
            if (active) plugin->deactivate(plugin);
            plugin->destroy(plugin);
        }
        if (entry) entry->deinit();
        close_library(library);
    }
};

ClapPluginInstance::ClapPluginInstance(std::unique_ptr<Impl> implementation)
    : impl_(std::move(implementation)) {}
ClapPluginInstance::~ClapPluginInstance() = default;
ClapPluginInstance::ClapPluginInstance(ClapPluginInstance&&) noexcept = default;
ClapPluginInstance& ClapPluginInstance::operator=(ClapPluginInstance&&) noexcept = default;

std::unique_ptr<ClapPluginInstance> ClapPluginInstance::create(
    const std::filesystem::path& path, const std::string& plugin_id, std::string* error) {
    auto fail = [&](const char* message) -> std::unique_ptr<ClapPluginInstance> {
        if (error) *error = message;
        return nullptr;
    };
    auto impl = std::make_unique<Impl>();
    impl->library = open_library(path);
    if (!impl->library) return fail("could not load CLAP library");
    impl->entry = find_entry(impl->library);
    if (!impl->entry || !clap_version_is_compatible(impl->entry->clap_version))
        return fail("missing or incompatible clap_entry");
    if (!impl->entry->init(path.string().c_str())) {
        impl->entry = nullptr;
        return fail("CLAP entry initialization failed");
    }
    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        impl->entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (!factory) return fail("CLAP plugin factory unavailable");
    impl->plugin = factory->create_plugin(factory, &impl->host, plugin_id.c_str());
    if (!impl->plugin) return fail("CLAP plugin could not be created");
    if (!impl->plugin->init(impl->plugin)) return fail("CLAP plugin initialization failed");
    impl->plugin_id = impl->plugin->desc->id;
    impl->plugin_name = impl->plugin->desc->name;
    impl->read_ports();
    return std::unique_ptr<ClapPluginInstance>(new ClapPluginInstance(std::move(impl)));
}

bool ClapPluginInstance::activate(double sample_rate, std::uint32_t min_frames,
                                  std::uint32_t max_frames) {
    if (!impl_ || !impl_->plugin || min_frames == 0 || max_frames < min_frames) return false;
    if (impl_->processing) { impl_->plugin->stop_processing(impl_->plugin); impl_->processing = false; }
    if (impl_->active) { impl_->plugin->deactivate(impl_->plugin); impl_->active = false; }
    // Ports may change only while the plugin is inactive, so they are read
    // again here and the input copy is sized for the largest block.
    impl_->read_ports();
    impl_->input_left.assign(impl_->input_channels > 0 ? max_frames : 0, 0.0F);
    impl_->input_right.assign(impl_->input_channels > 1 ? max_frames : 0, 0.0F);
    impl_->active = impl_->plugin->activate(impl_->plugin, sample_rate, min_frames, max_frames);
    if (!impl_->active) return false;
    impl_->state.latency_announced.store(false, std::memory_order_relaxed);
    impl_->read_latency();
    impl_->read_tail();
    impl_->processing = impl_->plugin->start_processing(impl_->plugin);
    return impl_->processing;
}

void ClapPluginInstance::process(StereoBlock audio,
                                 std::span<const PluginEvent> events) noexcept {
    if (!impl_ || !impl_->processing || audio.left.size() != audio.right.size()) return;
    // A retuned note is two events, so the buffer holds room for both.
    constexpr std::size_t maximum_events = 1024;
    std::array<EventStorage, maximum_events> converted{};
    std::size_t count = 0;
    for (const auto& source : events) {
        if (count == maximum_events) break;
        clap_event_header_t header{0, source.sample_offset, CLAP_CORE_EVENT_SPACE_ID, 0, 0};
        if (source.type == PluginEvent::Type::note_on || source.type == PluginEvent::Type::note_off) {
            header.size = sizeof(clap_event_note_t);
            header.type = source.type == PluginEvent::Type::note_on ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF;
            converted[count++].note = {header, -1, 0, 0,
                static_cast<std::int16_t>(source.key_or_parameter), source.value};
            // CLAP tuning is said in semitones and belongs to the note it
            // retunes, so it follows the note-on at the same sample.
            if (source.type == PluginEvent::Type::note_on && source.cents != 0.0 &&
                count < maximum_events) {
                clap_event_header_t tuning{sizeof(clap_event_note_expression_t),
                                           source.sample_offset, CLAP_CORE_EVENT_SPACE_ID,
                                           CLAP_EVENT_NOTE_EXPRESSION, 0};
                converted[count++].expression = {tuning, CLAP_NOTE_EXPRESSION_TUNING, -1, 0, 0,
                    static_cast<std::int16_t>(source.key_or_parameter), source.cents / 100.0};
            }
        } else if (source.type == PluginEvent::Type::parameter_value) {
            header.size = sizeof(clap_event_param_value_t);
            header.type = CLAP_EVENT_PARAM_VALUE;
            converted[count++].value = {header, static_cast<clap_id>(source.key_or_parameter), nullptr,
                                      -1, -1, -1, -1, source.value};
        } else {
            header.size = sizeof(clap_event_param_mod_t);
            header.type = CLAP_EVENT_PARAM_MOD;
            converted[count++].modulation = {header, static_cast<clap_id>(source.key_or_parameter), nullptr,
                                           -1, -1, -1, -1, source.value};
        }
    }
    InputEvents input{};
    input.api.ctx = &input;
    input.api.size = &input_size;
    input.api.get = &input_get;
    input.events = converted.data();
    input.count = static_cast<std::uint32_t>(count);

    const auto frames = audio.left.size();
    float* input_channels[] = {impl_->input_left.data(), impl_->input_right.data()};
    clap_audio_buffer_t audio_input{input_channels, nullptr, impl_->input_channels, 0, 0};
    if (impl_->input_channels > 0) {
        if (frames > impl_->input_left.size()) {
            // Larger than activate() promised: nothing safe to hand over.
            std::fill(audio.left.begin(), audio.left.end(), 0.0F);
            std::fill(audio.right.begin(), audio.right.end(), 0.0F);
            return;
        }
        if (impl_->input_channels == 2) {
            std::copy(audio.left.begin(), audio.left.end(), impl_->input_left.begin());
            std::copy(audio.right.begin(), audio.right.end(), impl_->input_right.begin());
        } else {
            for (std::size_t frame = 0; frame < frames; ++frame)
                impl_->input_left[frame] = 0.5F * (audio.left[frame] + audio.right[frame]);
        }
    }
    float* channels[] = {audio.left.data(), audio.right.data()};
    clap_audio_buffer_t audio_output{channels, nullptr, impl_->output_channels, 0, 0};
    const bool has_input = impl_->input_channels > 0;
    const bool has_output = impl_->output_channels > 0;
    clap_process_t process{impl_->steady_time, static_cast<std::uint32_t>(frames),
                           impl_->has_transport ? &impl_->transport : nullptr,
                           has_input ? &audio_input : nullptr,
                           has_output ? &audio_output : nullptr,
                           has_input ? 1U : 0U, has_output ? 1U : 0U,
                           &input.api, &impl_->output};
    // A flush the plugin asked for is served by this very call.
    impl_->state.flush_requested.store(false, std::memory_order_relaxed);
    clap_process_status status = CLAP_PROCESS_ERROR;
    {
        const AudioThreadScope audio_thread;
        status = impl_->plugin->process(impl_->plugin, &process);
    }
    if (status == CLAP_PROCESS_ERROR) {
        std::fill(audio.left.begin(), audio.left.end(), 0.0F);
        std::fill(audio.right.begin(), audio.right.end(), 0.0F);
    } else if (impl_->output_channels == 1) {
        std::copy(audio.left.begin(), audio.left.end(), audio.right.begin());
    }
    impl_->steady_time += static_cast<std::int64_t>(frames);
}

std::vector<std::byte> ClapPluginInstance::save_state() {
    std::vector<std::byte> bytes;
    if (!impl_ || !impl_->plugin) return bytes;
    const auto* state = impl_->extension<clap_plugin_state_t>(CLAP_EXT_STATE);
    if (!state) return bytes;
    VectorOutput output{};
    output.stream.ctx = &output;
    output.stream.write = write_bytes;
    output.bytes = &bytes;
    if (!state->save(impl_->plugin, &output.stream)) bytes.clear();
    return bytes;
}

bool ClapPluginInstance::load_state(std::span<const std::byte> bytes) {
    if (!impl_ || !impl_->plugin) return false;
    const auto* state = impl_->extension<clap_plugin_state_t>(CLAP_EXT_STATE);
    if (!state) return false;
    VectorInput input{};
    input.stream.ctx = &input;
    input.stream.read = read_bytes;
    input.bytes = bytes;
    input.cursor = 0;
    return state->load(impl_->plugin, &input.stream);
}

PluginPorts ClapPluginInstance::ports() const {
    if (!impl_) return {};
    return {impl_->input_channels, impl_->note_input};
}

std::uint32_t ClapPluginInstance::latency_samples() const noexcept {
    return impl_ ? impl_->latency.load(std::memory_order_acquire) : 0;
}

std::uint64_t ClapPluginInstance::tail_samples() const noexcept {
    return impl_ ? impl_->tail.load(std::memory_order_acquire) : 0;
}

bool ClapPluginInstance::latency_changed() noexcept {
    if (!impl_ || !impl_->state.latency_announced.exchange(false, std::memory_order_acq_rel))
        return false;
    // CLAP lets the host read latency only while the plugin is active; an
    // inactive plugin's new value is read when it is next activated.
    if (impl_->active) impl_->read_latency();
    return true;
}

std::vector<ParameterInfo> ClapPluginInstance::parameters() const {
    std::vector<ParameterInfo> result;
    if (!impl_) return result;
    const auto* params = impl_->extension<clap_plugin_params_t>(CLAP_EXT_PARAMS);
    if (!params) return result;
    const auto count = params->count(impl_->plugin);
    result.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        clap_param_info_t info{};
        if (!params->get_info(impl_->plugin, index, &info)) continue;
        result.push_back({static_cast<std::int32_t>(info.id),
                          std::string(info.name, strnlen(info.name, sizeof info.name)),
                          info.min_value, info.max_value, info.default_value,
                          (info.flags & CLAP_PARAM_IS_AUTOMATABLE) != 0});
    }
    return result;
}

std::size_t ClapPluginInstance::take_parameter_edits(std::span<ParameterEdit> out) noexcept {
    if (!impl_) return 0;
    std::size_t count = 0;
    while (count < out.size() && impl_->state.edits.pop(out[count])) ++count;
    return count;
}

void ClapPluginInstance::set_transport(const TransportInfo& info) noexcept {
    if (!impl_) return;
    auto& transport = impl_->transport;
    transport = {};
    transport.header = {sizeof(clap_event_transport_t), 0, CLAP_CORE_EVENT_SPACE_ID,
                        CLAP_EVENT_TRANSPORT, 0};
    transport.flags = CLAP_TRANSPORT_HAS_TEMPO | CLAP_TRANSPORT_HAS_BEATS_TIMELINE |
                      CLAP_TRANSPORT_HAS_TIME_SIGNATURE |
                      (info.playing ? static_cast<std::uint32_t>(CLAP_TRANSPORT_IS_PLAYING) : 0U);
    transport.song_pos_beats = static_cast<clap_beattime>(
        std::llround(info.beat * static_cast<double>(CLAP_BEATTIME_FACTOR)));
    transport.tempo = info.bpm;
    transport.bar_number = info.bar;
    transport.tsig_num = static_cast<std::uint16_t>(info.numerator);
    transport.tsig_denom = static_cast<std::uint16_t>(info.denominator);
    impl_->has_transport = true;
}

void ClapPluginInstance::idle() {
    if (!impl_ || !impl_->plugin) return;
    auto& state = impl_->state;
    if (state.callback_requested.exchange(false, std::memory_order_acq_rel))
        impl_->plugin->on_main_thread(impl_->plugin);
    // While the plugin is active only the audio thread may flush it, and
    // process() does. An inactive plugin is flushed here, on the main thread.
    if (!impl_->active && state.flush_requested.exchange(false, std::memory_order_acq_rel)) {
        if (const auto* params = impl_->extension<clap_plugin_params_t>(CLAP_EXT_PARAMS)) {
            const clap_input_events_t none{nullptr, empty_size, empty_get};
            params->flush(impl_->plugin, &none, &impl_->output);
        }
    }
    if (state.tail_announced.exchange(false, std::memory_order_acq_rel)) impl_->read_tail();
    state.parameters_rescanned.store(false, std::memory_order_relaxed);
}

const std::string& ClapPluginInstance::id() const noexcept { return impl_->plugin_id; }
const std::string& ClapPluginInstance::name() const noexcept { return impl_->plugin_name; }

} // namespace blokkily
