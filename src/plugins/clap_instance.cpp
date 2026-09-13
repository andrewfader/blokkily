#include "blokkily/plugins/clap_instance.hpp"

#include <clap/clap.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <array>
#include <cstring>

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

const void* CLAP_ABI host_extension(const clap_host_t*, const char*) { return nullptr; }
void CLAP_ABI host_restart(const clap_host_t*) {}
void CLAP_ABI host_process(const clap_host_t*) {}
void CLAP_ABI host_callback(const clap_host_t*) {}

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
bool CLAP_ABI discard_output(const clap_output_events_t*, const clap_event_header_t*) {
    return true;
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
    clap_host_t host{CLAP_VERSION, this, "Blokkily", "Blokkily", "https://blokkily.invalid",
                     "0.1.0", host_extension, host_restart, host_process, host_callback};
    std::string plugin_id;
    std::string plugin_name;
    bool active = false;
    bool processing = false;
    std::int64_t steady_time = 0;

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
    return std::unique_ptr<ClapPluginInstance>(new ClapPluginInstance(std::move(impl)));
}

bool ClapPluginInstance::activate(double sample_rate, std::uint32_t min_frames,
                                  std::uint32_t max_frames) {
    if (!impl_ || !impl_->plugin || min_frames == 0 || max_frames < min_frames) return false;
    if (impl_->processing) { impl_->plugin->stop_processing(impl_->plugin); impl_->processing = false; }
    if (impl_->active) { impl_->plugin->deactivate(impl_->plugin); impl_->active = false; }
    impl_->active = impl_->plugin->activate(impl_->plugin, sample_rate, min_frames, max_frames);
    if (!impl_->active) return false;
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
    clap_output_events_t output{nullptr, discard_output};
    float* channels[] = {audio.left.data(), audio.right.data()};
    clap_audio_buffer_t audio_output{channels, nullptr, 2, 0, 0};
    clap_process_t process{impl_->steady_time, static_cast<std::uint32_t>(audio.left.size()), nullptr,
                           nullptr, &audio_output, 0, 1, &input.api, &output};
    if (impl_->plugin->process(impl_->plugin, &process) == CLAP_PROCESS_ERROR) {
        std::fill(audio.left.begin(), audio.left.end(), 0.0F);
        std::fill(audio.right.begin(), audio.right.end(), 0.0F);
    }
    impl_->steady_time += static_cast<std::int64_t>(audio.left.size());
}

std::vector<std::byte> ClapPluginInstance::save_state() {
    std::vector<std::byte> bytes;
    if (!impl_ || !impl_->plugin) return bytes;
    const auto* state = static_cast<const clap_plugin_state_t*>(
        impl_->plugin->get_extension(impl_->plugin, CLAP_EXT_STATE));
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
    const auto* state = static_cast<const clap_plugin_state_t*>(
        impl_->plugin->get_extension(impl_->plugin, CLAP_EXT_STATE));
    if (!state) return false;
    VectorInput input{};
    input.stream.ctx = &input;
    input.stream.read = read_bytes;
    input.bytes = bytes;
    input.cursor = 0;
    return state->load(impl_->plugin, &input.stream);
}

const std::string& ClapPluginInstance::id() const noexcept { return impl_->plugin_id; }
const std::string& ClapPluginInstance::name() const noexcept { return impl_->plugin_name; }

} // namespace blokkily
