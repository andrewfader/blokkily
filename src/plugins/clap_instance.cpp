#include "blokkily/plugins/clap_instance.hpp"

#include "blokkily/audio/event_queue.hpp"
#include "blokkily/plugins/plugin_run_loop.hpp"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/latency.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/posix-fd-support.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>
#include <clap/ext/thread-check.h>
#include <clap/ext/timer-support.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <limits>
#include <thread>
#include <utility>
#include <vector>

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

    // --- Main-thread services (item 2.6) ------------------------------------
    // The plugin, once created, so run-loop callbacks can reach it.
    const clap_plugin_t* plugin = nullptr;
    // Timers and descriptors the plugin registered, with the run loop each
    // went to, so every one is removed before the plugin is destroyed.
    struct TimerRegistration {
        clap_id id;
        PluginRunLoop* loop;
        std::uint64_t loop_id;
    };
    std::vector<TimerRegistration> timers;
    clap_id next_timer = 1;
    struct FdRegistration {
        int fd;
        PluginRunLoop* loop;
    };
    std::vector<FdRegistration> fds;
    // The editor, when one is open, and what its window was asked for from
    // threads other than the main one, served by idle().
    // Read by the gui callbacks, which may arrive on any thread.
    EditorHost* editor_host = nullptr;
    std::atomic<bool> editor_open{false};
    std::atomic<bool> editor_floating{false};
    std::atomic<bool> gui_closed{false};
    std::atomic<bool> gui_show_requested{false};
    std::atomic<bool> gui_hide_requested{false};
    std::atomic<bool> gui_resize_requested{false};
    std::atomic<std::uint32_t> gui_resize_width{0};
    std::atomic<std::uint32_t> gui_resize_height{0};

    [[nodiscard]] bool on_main_thread() const noexcept {
        return std::this_thread::get_id() == main_thread;
    }
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

// --- clap.gui: what an open editor asks of the window hosting it ------------
void CLAP_ABI gui_resize_hints_changed(const clap_host_t*) {}
bool CLAP_ABI gui_request_resize(const clap_host_t* host, std::uint32_t width,
                                 std::uint32_t height) {
    auto* state = state_of(host);
    if (!state->editor_open || state->editor_floating) return false;
    if (state->on_main_thread() && state->editor_host != nullptr) {
        state->editor_host->request_resize(width, height);
        return true;
    }
    state->gui_resize_width.store(width, std::memory_order_relaxed);
    state->gui_resize_height.store(height, std::memory_order_relaxed);
    state->gui_resize_requested.store(true, std::memory_order_release);
    return true;
}
bool CLAP_ABI gui_request_show(const clap_host_t* host) {
    auto* state = state_of(host);
    if (!state->editor_open) return false;
    if (state->on_main_thread() && state->editor_host != nullptr) state->editor_host->request_show();
    else state->gui_show_requested.store(true, std::memory_order_release);
    return true;
}
bool CLAP_ABI gui_request_hide(const clap_host_t* host) {
    auto* state = state_of(host);
    if (!state->editor_open) return false;
    if (state->on_main_thread() && state->editor_host != nullptr) state->editor_host->request_hide();
    else state->gui_hide_requested.store(true, std::memory_order_release);
    return true;
}
// Whatever thread it arrives on, and whether or not the plugin already
// destroyed its window, the editor is taken down on the main thread by idle():
// calling back into the plugin from inside its own call would re-enter it.
void CLAP_ABI gui_closed(const clap_host_t* host, bool) {
    state_of(host)->gui_closed.store(true, std::memory_order_release);
}
constexpr clap_host_gui_t host_gui{gui_resize_hints_changed, gui_request_resize,
                                   gui_request_show, gui_request_hide, gui_closed};

// --- clap.timer-support and clap.posix-fd-support ------------------------------
// Both go to whichever run loop the application installed; with none, the
// plugin is told no and has to manage without.
bool CLAP_ABI timer_register(const clap_host_t* host, std::uint32_t period_ms, clap_id* timer_id) {
    auto* state = state_of(host);
    auto* loop = plugin_run_loop();
    if (loop == nullptr || timer_id == nullptr || !state->on_main_thread()) return false;
    const clap_id id = state->next_timer++;
    const auto loop_id = loop->add_timer(period_ms, [state, id] {
        if (state->plugin == nullptr) return;
        const auto* timers = static_cast<const clap_plugin_timer_support_t*>(
            state->plugin->get_extension(state->plugin, CLAP_EXT_TIMER_SUPPORT));
        if (timers != nullptr && timers->on_timer != nullptr) timers->on_timer(state->plugin, id);
    });
    if (loop_id == 0) return false;
    state->timers.push_back({id, loop, loop_id});
    *timer_id = id;
    return true;
}
bool CLAP_ABI timer_unregister(const clap_host_t* host, clap_id timer_id) {
    auto* state = state_of(host);
    const auto found = std::find_if(state->timers.begin(), state->timers.end(),
                                    [timer_id](const auto& timer) { return timer.id == timer_id; });
    if (found == state->timers.end()) return false;
    (void)found->loop->remove_timer(found->loop_id);
    state->timers.erase(found);
    return true;
}
constexpr clap_host_timer_support_t host_timer_support{timer_register, timer_unregister};

bool CLAP_ABI fd_register(const clap_host_t* host, int fd, clap_posix_fd_flags_t flags) {
    auto* state = state_of(host);
    auto* loop = plugin_run_loop();
    if (loop == nullptr || !state->on_main_thread()) return false;
    const bool added = loop->add_fd(fd, flags, [state](int ready, std::uint32_t events) {
        if (state->plugin == nullptr) return;
        const auto* watcher = static_cast<const clap_plugin_posix_fd_support_t*>(
            state->plugin->get_extension(state->plugin, CLAP_EXT_POSIX_FD_SUPPORT));
        if (watcher != nullptr && watcher->on_fd != nullptr)
            watcher->on_fd(state->plugin, ready, events);
    });
    if (added) state->fds.push_back({fd, loop});
    return added;
}
bool CLAP_ABI fd_modify(const clap_host_t* host, int fd, clap_posix_fd_flags_t flags) {
    auto* state = state_of(host);
    const auto found = std::find_if(state->fds.begin(), state->fds.end(),
                                    [fd](const auto& watch) { return watch.fd == fd; });
    return found != state->fds.end() && found->loop->modify_fd(fd, flags);
}
bool CLAP_ABI fd_unregister(const clap_host_t* host, int fd) {
    auto* state = state_of(host);
    const auto found = std::find_if(state->fds.begin(), state->fds.end(),
                                    [fd](const auto& watch) { return watch.fd == fd; });
    if (found == state->fds.end()) return false;
    (void)found->loop->remove_fd(fd);
    state->fds.erase(found);
    return true;
}
constexpr clap_host_posix_fd_support_t host_posix_fd{fd_register, fd_modify, fd_unregister};

const char* window_api_name(WindowApi api) {
    switch (api) {
    case WindowApi::x11: return CLAP_WINDOW_API_X11;
    case WindowApi::wayland: return CLAP_WINDOW_API_WAYLAND;
    case WindowApi::win32: return CLAP_WINDOW_API_WIN32;
    case WindowApi::cocoa: return CLAP_WINDOW_API_COCOA;
    }
    return CLAP_WINDOW_API_X11;
}

const void* CLAP_ABI host_extension(const clap_host_t*, const char* id) {
    if (id == nullptr) return nullptr;
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &host_gui;
    if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0) return &host_timer_support;
    if (std::strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT) == 0) return &host_posix_fd;
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

    [[nodiscard]] const clap_plugin_gui_t* gui() const {
        return extension<clap_plugin_gui_t>(CLAP_EXT_GUI);
    }

    // Takes the editor down: hidden, then destroyed, as CLAP orders it.
    void destroy_editor() {
        if (!state.editor_open) return;
        if (const auto* plugin_gui = gui()) {
            (void)plugin_gui->hide(plugin);
            plugin_gui->destroy(plugin);
        }
        state.editor_open = false;
        state.editor_host = nullptr;
        state.gui_closed.store(false, std::memory_order_relaxed);
    }

    // Whatever the plugin still has registered is removed from the run loop,
    // so no callback can reach a plugin that is gone.
    void release_run_loop() {
        for (const auto& timer : state.timers) (void)timer.loop->remove_timer(timer.loop_id);
        state.timers.clear();
        for (const auto& watch : state.fds) (void)watch.loop->remove_fd(watch.fd);
        state.fds.clear();
    }

    ~Impl() {
        if (plugin) {
            // CLAP destroys the editor before the plugin, and the window that
            // hosted it is told, so it can go too.
            if (state.editor_open) {
                auto* host_window = state.editor_host;
                destroy_editor();
                if (host_window != nullptr) host_window->closed();
            }
            if (processing) plugin->stop_processing(plugin);
            if (active) plugin->deactivate(plugin);
            plugin->destroy(plugin);
        }
        state.plugin = nullptr;
        release_run_loop();
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
    impl->state.plugin = impl->plugin;
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

void ClapPluginInstance::reset() {
    if (!impl_ || !impl_->plugin || !impl_->active) return;
    // [audio-thread & active]: the thread calling this processes next.
    const AudioThreadScope audio_thread;
    impl_->plugin->reset(impl_->plugin);
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
    // What the editor asked of its window from another thread.
    if (state.editor_open && state.editor_host != nullptr) {
        if (state.gui_resize_requested.exchange(false, std::memory_order_acq_rel))
            state.editor_host->request_resize(state.gui_resize_width.load(std::memory_order_relaxed),
                                              state.gui_resize_height.load(std::memory_order_relaxed));
        if (state.gui_show_requested.exchange(false, std::memory_order_acq_rel))
            state.editor_host->request_show();
        if (state.gui_hide_requested.exchange(false, std::memory_order_acq_rel))
            state.editor_host->request_hide();
    }
    // The plugin closed its own editor: CLAP asks the host to acknowledge by
    // destroying it, and the window that hosted it goes too.
    if (state.gui_closed.exchange(false, std::memory_order_acq_rel) && state.editor_open) {
        auto* host_window = state.editor_host;
        impl_->destroy_editor();
        if (host_window != nullptr) host_window->closed();
    }
}

bool ClapPluginInstance::has_editor() const { return impl_ && impl_->gui() != nullptr; }

bool ClapPluginInstance::supports_editor(WindowApi api, bool floating) const {
    const auto* plugin_gui = impl_ ? impl_->gui() : nullptr;
    return plugin_gui != nullptr &&
           plugin_gui->is_api_supported(impl_->plugin, window_api_name(api), floating);
}

bool ClapPluginInstance::open_editor(const NativeParent* parent, EditorHost& host,
                                     EditorSize* size, std::string* error) {
    auto fail = [error](const char* message) {
        if (error != nullptr) *error = message;
        return false;
    };
    if (!impl_ || !impl_->plugin) return fail("No plugin");
    if (impl_->state.editor_open) return fail("The editor is already open");
    const auto* plugin_gui = impl_->gui();
    if (plugin_gui == nullptr) return fail("This plugin has no editor");
    const bool floating = parent == nullptr;
    // A floating window is the plugin's own top-level; on this platform that
    // is an X11 window.
    const char* api = floating ? CLAP_WINDOW_API_X11 : window_api_name(parent->api);
    if (!plugin_gui->is_api_supported(impl_->plugin, api, floating))
        return fail(floating ? "The plugin cannot open a floating editor"
                             : "The plugin cannot embed its editor in this window");
    // The editor counts as open from create() on, so a resize or show the
    // plugin requests while it builds its window is served.
    impl_->state.editor_host = &host;
    impl_->state.editor_floating = floating;
    impl_->state.editor_open = true;
    impl_->state.gui_closed.store(false, std::memory_order_relaxed);
    if (!plugin_gui->create(impl_->plugin, api, floating)) {
        impl_->state.editor_open = false;
        impl_->state.editor_host = nullptr;
        return fail("The plugin could not create its editor");
    }
    EditorSize measured;
    if (floating) {
        plugin_gui->suggest_title(impl_->plugin, impl_->plugin_name.c_str());
    } else {
        (void)plugin_gui->set_scale(impl_->plugin, parent->scale);
        measured.resizable = plugin_gui->can_resize(impl_->plugin);
        std::uint32_t width = 0, height = 0;
        if (plugin_gui->get_size(impl_->plugin, &width, &height)) {
            measured.width = width;
            measured.height = height;
        }
        clap_window_t window{};
        window.api = api;
        if (parent->api == WindowApi::x11) window.x11 = static_cast<clap_xwnd>(parent->handle);
        else window.ptr = reinterpret_cast<void*>(parent->handle);
        if (!plugin_gui->set_parent(impl_->plugin, &window)) {
            impl_->destroy_editor();
            return fail("The plugin refused the window it was given");
        }
    }
    if (!plugin_gui->show(impl_->plugin)) {
        impl_->destroy_editor();
        return fail("The plugin could not show its editor");
    }
    if (size != nullptr) *size = measured;
    return true;
}

bool ClapPluginInstance::resize_editor(std::uint32_t& width, std::uint32_t& height) {
    const auto* plugin_gui = impl_ ? impl_->gui() : nullptr;
    if (plugin_gui == nullptr || !impl_->state.editor_open || impl_->state.editor_floating ||
        !plugin_gui->can_resize(impl_->plugin))
        return false;
    (void)plugin_gui->adjust_size(impl_->plugin, &width, &height);
    return plugin_gui->set_size(impl_->plugin, width, height);
}

void ClapPluginInstance::set_editor_scale(double scale) {
    const auto* plugin_gui = impl_ ? impl_->gui() : nullptr;
    if (plugin_gui != nullptr && impl_->state.editor_open && !impl_->state.editor_floating)
        (void)plugin_gui->set_scale(impl_->plugin, scale);
}

void ClapPluginInstance::close_editor() {
    if (impl_) impl_->destroy_editor();
}

bool ClapPluginInstance::editor_open() const { return impl_ && impl_->state.editor_open; }

const std::string& ClapPluginInstance::id() const noexcept { return impl_->plugin_id; }
const std::string& ClapPluginInstance::name() const noexcept { return impl_->plugin_name; }

} // namespace blokkily
