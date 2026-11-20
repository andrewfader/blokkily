// A minimal CLAP audio effect: a stereo main input and a stereo main output,
// the output being the input at half level. It proves that the host wires the
// block it is given into the plugin's audio input, through the official ABI.
// It is built into a directory of its own, so scans of the instrument fixture
// directory never meet it. Built with BLOKKILY_MONO_FIXTURE, both ports are
// mono, as a DPF effect with one input and one output declares them.

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <new>

namespace {
constexpr const char* features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, nullptr};
#ifdef BLOKKILY_MONO_FIXTURE
constexpr std::uint32_t channels = 1;
constexpr clap_plugin_descriptor_t descriptor{
    CLAP_VERSION, "dev.blokkily.test.mono", "Blokkily Test Mono", "Blokkily",
    "https://blokkily.invalid", "", "", "0.1.0", "Test fixture: halves its mono input", features};
#else
constexpr std::uint32_t channels = 2;
constexpr clap_plugin_descriptor_t descriptor{
    CLAP_VERSION, "dev.blokkily.test.input", "Blokkily Test Input", "Blokkily",
    "https://blokkily.invalid", "", "", "0.1.0", "Test fixture: halves its input", features};
#endif

// Whether any process() call was handed an input buffer that is the output
// buffer: the ports do not pair for in-place use, so a host must not do that.
std::atomic<bool> aliased{false};

bool plugin_init(const clap_plugin_t*) { return true; }
void plugin_destroy(const clap_plugin_t* plugin) { delete static_cast<clap_plugin_t*>(plugin->plugin_data); }
bool plugin_activate(const clap_plugin_t*, double, std::uint32_t, std::uint32_t) { return true; }
void plugin_deactivate(const clap_plugin_t*) {}
bool plugin_start(const clap_plugin_t*) { return true; }
void plugin_stop(const clap_plugin_t*) {}
void plugin_reset(const clap_plugin_t*) {}

clap_process_status plugin_process(const clap_plugin_t*, const clap_process_t* process) {
    if (process->audio_inputs_count != 1 || process->audio_outputs_count != 1 ||
        process->audio_inputs[0].channel_count != channels ||
        process->audio_outputs[0].channel_count != channels)
        return CLAP_PROCESS_ERROR;
    auto** in = process->audio_inputs[0].data32;
    auto** out = process->audio_outputs[0].data32;
    for (std::uint32_t channel = 0; channel < channels; ++channel)
        if (in[channel] == out[channel]) aliased = true;
    for (std::uint32_t channel = 0; channel < channels; ++channel)
        for (std::uint32_t frame = 0; frame < process->frames_count; ++frame)
            out[channel][frame] = 0.5F * in[channel][frame];
    return CLAP_PROCESS_CONTINUE;
}

std::uint32_t ports_count(const clap_plugin_t*, bool) { return 1; }
bool ports_get(const clap_plugin_t*, std::uint32_t index, bool is_input,
               clap_audio_port_info_t* info) {
    if (index != 0) return false;
    *info = {};
    info->id = 0;
    std::snprintf(info->name, sizeof info->name, "%s", is_input ? "Input" : "Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = channels;
    info->port_type = channels == 1 ? CLAP_PORT_MONO : CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
constexpr clap_plugin_audio_ports_t audio_ports{ports_count, ports_get};

const void* plugin_extension(const clap_plugin_t*, const char* id) {
    return std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0 ? &audio_ports : nullptr;
}
void plugin_main_thread(const clap_plugin_t*) {}

std::uint32_t count(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* describe(const clap_plugin_factory_t*, std::uint32_t index) {
    return index == 0 ? &descriptor : nullptr;
}
const clap_plugin_t* create(const clap_plugin_factory_t*, const clap_host_t*, const char* id) {
    if (id == nullptr || std::strcmp(id, descriptor.id) != 0) return nullptr;
    auto* plugin = new (std::nothrow) clap_plugin_t{};
    if (plugin == nullptr) return nullptr;
    *plugin = {&descriptor, plugin, plugin_init, plugin_destroy, plugin_activate,
               plugin_deactivate, plugin_start, plugin_stop, plugin_reset,
               plugin_process, plugin_extension, plugin_main_thread};
    return plugin;
}
constexpr clap_plugin_factory_t factory{count, describe, create};
bool entry_init(const char*) { return true; }
void entry_deinit() {}
const void* get_factory(const char* id) {
    return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
}
} // namespace

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION, entry_init, entry_deinit, get_factory};

extern "C" CLAP_EXPORT bool blokkily_test_input_aliased() { return aliased; }
