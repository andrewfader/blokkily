#include <clap/clap.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <cstring>
#include <new>

namespace {
constexpr const char* features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT,
                                    CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr};
constexpr clap_plugin_descriptor_t descriptor{
    CLAP_VERSION, "dev.blokkily.test", "Blokkily Test Synth", "Blokkily",
    "https://blokkily.invalid", "", "", "0.1.0", "Test fixture", features};

struct TestSynth { clap_plugin_t plugin; bool sounding = false; float level = 0.25F; };
TestSynth* self(const clap_plugin_t* plugin) { return static_cast<TestSynth*>(plugin->plugin_data); }
bool plugin_init(const clap_plugin_t*) { return true; }
void plugin_destroy(const clap_plugin_t* plugin) { delete self(plugin); }
bool plugin_activate(const clap_plugin_t*, double, std::uint32_t, std::uint32_t) { return true; }
void plugin_deactivate(const clap_plugin_t*) {}
bool plugin_start(const clap_plugin_t*) { return true; }
void plugin_stop(const clap_plugin_t*) {}
void plugin_reset(const clap_plugin_t* plugin) { self(plugin)->sounding = false; }

clap_process_status plugin_process(const clap_plugin_t* plugin, const clap_process_t* process) {
    auto* synth = self(plugin);
    if (process->audio_outputs_count != 1 || process->audio_outputs[0].channel_count != 2)
        return CLAP_PROCESS_ERROR;
    auto** channels = process->audio_outputs[0].data32;
    std::uint32_t cursor = 0;
    const auto render_to = [&](std::uint32_t end) {
        for (; cursor < end; ++cursor)
            channels[0][cursor] = channels[1][cursor] = synth->sounding ? synth->level : 0.0F;
    };
    const auto event_count = process->in_events->size(process->in_events);
    for (std::uint32_t index = 0; index < event_count; ++index) {
        const auto* header = process->in_events->get(process->in_events, index);
        render_to(std::min(header->time, process->frames_count));
        if (header->space_id == CLAP_CORE_EVENT_SPACE_ID && header->type == CLAP_EVENT_NOTE_ON)
            synth->sounding = true;
        else if (header->space_id == CLAP_CORE_EVENT_SPACE_ID && header->type == CLAP_EVENT_NOTE_OFF)
            synth->sounding = false;
        else if (header->space_id == CLAP_CORE_EVENT_SPACE_ID && header->type == CLAP_EVENT_PARAM_VALUE)
            synth->level = static_cast<float>(reinterpret_cast<const clap_event_param_value_t*>(header)->value);
        else if (header->space_id == CLAP_CORE_EVENT_SPACE_ID && header->type == CLAP_EVENT_PARAM_MOD)
            synth->level += static_cast<float>(reinterpret_cast<const clap_event_param_mod_t*>(header)->amount);
    }
    render_to(process->frames_count);
    return CLAP_PROCESS_CONTINUE;
}

bool save_state(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
    const auto* synth = self(plugin);
    return stream->write(stream, &synth->level, sizeof(synth->level)) == sizeof(synth->level);
}
bool load_state(const clap_plugin_t* plugin, const clap_istream_t* stream) {
    auto* synth = self(plugin);
    return stream->read(stream, &synth->level, sizeof(synth->level)) == sizeof(synth->level);
}
constexpr clap_plugin_state_t state_extension{save_state, load_state};
const void* plugin_extension(const clap_plugin_t*, const char* id) {
    return std::strcmp(id, CLAP_EXT_STATE) == 0 ? &state_extension : nullptr;
}
void plugin_main_thread(const clap_plugin_t*) {}

std::uint32_t count(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* describe(const clap_plugin_factory_t*, std::uint32_t index) {
    return index == 0 ? &descriptor : nullptr;
}
const clap_plugin_t* create(const clap_plugin_factory_t*, const clap_host_t*, const char* id) {
    if (id == nullptr || std::strcmp(id, descriptor.id) != 0) return nullptr;
    auto* synth = new (std::nothrow) TestSynth{};
    if (synth == nullptr) return nullptr;
    synth->plugin = {&descriptor, synth, plugin_init, plugin_destroy, plugin_activate,
                     plugin_deactivate, plugin_start, plugin_stop, plugin_reset,
                     plugin_process, plugin_extension, plugin_main_thread};
    return &synth->plugin;
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
