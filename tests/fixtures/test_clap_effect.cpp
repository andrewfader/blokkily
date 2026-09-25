// A CLAP audio effect whose work can be read straight off the rendered audio:
// the output is the input, delayed by the 64 samples it reports as its
// latency, times its Gain parameter (id 0, default 0.25). Gain changes land
// at the sample offset of their event, and modulation adds to the base. The
// tail it reports is the 64 samples still in its line once input stops, and
// its state stream holds a tag and the gain. Like an effect that has work for
// the main thread, it can ask its host for an on_main_thread callback
// (blokkily_test_effect_request_callback) and counts the ones it receives.
//
// Built by the suite and loaded through the official clap_entry, in a
// directory of its own so that instrument scans never meet it.

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/latency.h>
#include <clap/ext/note-ports.h>
#include <clap/ext/params.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <atomic>
#include <mutex>
#include <new>
#include <vector>

namespace {
constexpr const char* features[] = {CLAP_PLUGIN_FEATURE_AUDIO_EFFECT, nullptr};
constexpr clap_plugin_descriptor_t descriptor{
    CLAP_VERSION, "dev.blokkily.test.effect", "Blokkily Test Effect", "Blokkily",
    "https://blokkily.invalid", "", "", "0.1.0",
    "Test fixture: delays its input by 64 samples and scales it by Gain", features};

constexpr std::uint32_t latency = 64;
constexpr clap_id gain_id = 0;
constexpr double default_gain = 0.25;

struct Effect {
    clap_plugin_t plugin{};
    const clap_host_t* host = nullptr;
    double gain = default_gain;
    double modulation = 0.0;
    // Two ring buffers of exactly `latency` samples: reading the slot about
    // to be overwritten yields the input from `latency` samples ago.
    std::array<std::array<float, latency>, 2> line{};
    std::uint32_t cursor = 0;
};

Effect* self(const clap_plugin_t* plugin) { return static_cast<Effect*>(plugin->plugin_data); }

// Every live instance, so a test can ask each one's host for a callback.
std::mutex live_mutex;
std::vector<Effect*> live_effects;
std::atomic<int> main_thread_calls{0};

void apply(Effect& effect, const clap_event_header_t* header) {
    if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) return;
    if (header->type == CLAP_EVENT_PARAM_VALUE) {
        const auto* event = reinterpret_cast<const clap_event_param_value_t*>(header);
        if (event->param_id == gain_id) effect.gain = std::clamp(event->value, 0.0, 1.0);
    } else if (header->type == CLAP_EVENT_PARAM_MOD) {
        const auto* event = reinterpret_cast<const clap_event_param_mod_t*>(header);
        if (event->param_id == gain_id) effect.modulation = event->amount;
    }
}

bool plugin_init(const clap_plugin_t*) { return true; }
void plugin_destroy(const clap_plugin_t* plugin) {
    {
        const std::lock_guard lock(live_mutex);
        std::erase(live_effects, self(plugin));
    }
    delete self(plugin);
}
bool plugin_activate(const clap_plugin_t* plugin, double, std::uint32_t, std::uint32_t) {
    auto* effect = self(plugin);
    effect->line = {};
    effect->cursor = 0;
    effect->modulation = 0.0;
    return true;
}
void plugin_deactivate(const clap_plugin_t*) {}
bool plugin_start(const clap_plugin_t*) { return true; }
void plugin_stop(const clap_plugin_t*) {}
void plugin_reset(const clap_plugin_t* plugin) {
    self(plugin)->line = {};
    self(plugin)->cursor = 0;
}

clap_process_status plugin_process(const clap_plugin_t* plugin, const clap_process_t* process) {
    if (process->audio_inputs_count != 1 || process->audio_outputs_count != 1 ||
        process->audio_inputs[0].channel_count != 2 || process->audio_outputs[0].channel_count != 2)
        return CLAP_PROCESS_ERROR;
    auto& effect = *self(plugin);
    auto** in = process->audio_inputs[0].data32;
    auto** out = process->audio_outputs[0].data32;
    const std::uint32_t events = process->in_events->size(process->in_events);
    std::uint32_t next = 0;
    for (std::uint32_t frame = 0; frame < process->frames_count; ++frame) {
        while (next < events) {
            const auto* header = process->in_events->get(process->in_events, next);
            if (header->time > frame) break;
            apply(effect, header);
            ++next;
        }
        const auto gain = static_cast<float>(std::clamp(effect.gain + effect.modulation, 0.0, 1.0));
        for (std::uint32_t channel = 0; channel < 2; ++channel) {
            const float delayed = effect.line[channel][effect.cursor];
            effect.line[channel][effect.cursor] = in[channel][frame];
            out[channel][frame] = gain * delayed;
        }
        effect.cursor = (effect.cursor + 1) % latency;
    }
    for (; next < events; ++next) apply(effect, process->in_events->get(process->in_events, next));
    return CLAP_PROCESS_CONTINUE;
}

// --- clap.audio-ports: stereo in, stereo out, not paired for in-place use ---
std::uint32_t ports_count(const clap_plugin_t*, bool) { return 1; }
bool ports_get(const clap_plugin_t*, std::uint32_t index, bool is_input,
               clap_audio_port_info_t* info) {
    if (index != 0) return false;
    *info = {};
    info->id = 0;
    std::snprintf(info->name, sizeof info->name, "%s", is_input ? "Input" : "Output");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
constexpr clap_plugin_audio_ports_t audio_ports{ports_count, ports_get};

// --- clap.params ---------------------------------------------------------------
std::uint32_t params_count(const clap_plugin_t*) { return 1; }
bool params_info(const clap_plugin_t*, std::uint32_t index, clap_param_info_t* info) {
    if (index != 0) return false;
    *info = {};
    info->id = gain_id;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
    info->min_value = 0.0;
    info->max_value = 1.0;
    info->default_value = default_gain;
    std::snprintf(info->name, sizeof info->name, "%s", "Gain");
    return true;
}
bool params_value(const clap_plugin_t* plugin, clap_id id, double* out) {
    if (id != gain_id) return false;
    *out = self(plugin)->gain;
    return true;
}
bool params_to_text(const clap_plugin_t*, clap_id id, double value, char* text,
                    std::uint32_t size) {
    if (id != gain_id) return false;
    std::snprintf(text, size, "%.3f", value);
    return true;
}
bool params_from_text(const clap_plugin_t*, clap_id id, const char* text, double* value) {
    if (id != gain_id) return false;
    char* end = nullptr;
    *value = std::strtod(text, &end);
    return end != text;
}
void params_flush(const clap_plugin_t* plugin, const clap_input_events_t* in,
                  const clap_output_events_t*) {
    for (std::uint32_t index = 0; index < in->size(in); ++index)
        apply(*self(plugin), in->get(in, index));
}
constexpr clap_plugin_params_t params{params_count, params_info, params_value,
                                      params_to_text, params_from_text, params_flush};

// --- clap.state: a tag and the gain ----------------------------------------------
constexpr std::array<char, 4> state_tag{'B', 'K', 'F', 'E'};
struct State {
    std::array<char, 4> tag;
    double gain;
};
bool save_state(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
    const State state{state_tag, self(plugin)->gain};
    const auto* bytes = reinterpret_cast<const char*>(&state);
    std::size_t written = 0;
    while (written < sizeof state) {
        const auto count = stream->write(stream, bytes + written, sizeof state - written);
        if (count <= 0) return false;
        written += static_cast<std::size_t>(count);
    }
    return true;
}
bool load_state(const clap_plugin_t* plugin, const clap_istream_t* stream) {
    std::array<char, sizeof(State) + 1> bytes{};
    std::size_t read = 0;
    while (read < bytes.size()) {
        const auto count = stream->read(stream, bytes.data() + read, bytes.size() - read);
        if (count < 0) return false;
        if (count == 0) break;
        read += static_cast<std::size_t>(count);
    }
    State state{};
    if (read != sizeof state) return false;
    std::memcpy(&state, bytes.data(), sizeof state);
    if (state.tag != state_tag || !(state.gain >= 0.0 && state.gain <= 1.0)) return false;
    self(plugin)->gain = state.gain;
    return true;
}
constexpr clap_plugin_state_t state_extension{save_state, load_state};

std::uint32_t latency_get(const clap_plugin_t*) { return latency; }
constexpr clap_plugin_latency_t latency_extension{latency_get};
std::uint32_t tail_get(const clap_plugin_t*) { return latency; }
constexpr clap_plugin_tail_t tail_extension{tail_get};

// --- clap.note-ports: none, it is an effect ---------------------------------------
std::uint32_t note_ports_count(const clap_plugin_t*, bool) { return 0; }
bool note_ports_get(const clap_plugin_t*, std::uint32_t, bool, clap_note_port_info_t*) {
    return false;
}
constexpr clap_plugin_note_ports_t note_ports{note_ports_count, note_ports_get};

const void* plugin_extension(const clap_plugin_t*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audio_ports;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0) return &note_ports;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &params;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &state_extension;
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &latency_extension;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tail_extension;
    return nullptr;
}
void plugin_main_thread(const clap_plugin_t*) { ++main_thread_calls; }

std::uint32_t count(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* describe(const clap_plugin_factory_t*, std::uint32_t index) {
    return index == 0 ? &descriptor : nullptr;
}
const clap_plugin_t* create(const clap_plugin_factory_t*, const clap_host_t* host,
                           const char* id) {
    if (id == nullptr || std::strcmp(id, descriptor.id) != 0) return nullptr;
    auto* effect = new (std::nothrow) Effect{};
    if (effect == nullptr) return nullptr;
    effect->host = host;
    {
        const std::lock_guard lock(live_mutex);
        live_effects.push_back(effect);
    }
    effect->plugin = {&descriptor, effect, plugin_init, plugin_destroy, plugin_activate,
                      plugin_deactivate, plugin_start, plugin_stop, plugin_reset,
                      plugin_process, plugin_extension, plugin_main_thread};
    return &effect->plugin;
}
constexpr clap_plugin_factory_t factory{count, describe, create};
bool entry_init(const char*) { return true; }
void entry_deinit() {}
const void* get_factory(const char* id) {
    return std::strcmp(id, CLAP_PLUGIN_FACTORY_ID) == 0 ? &factory : nullptr;
}
} // namespace

// Asks every live instance's host for an on_main_thread callback, as an effect
// with main-thread work does. Safe from any thread, as request_callback is.
extern "C" CLAP_EXPORT void blokkily_test_effect_request_callback() {
    const std::lock_guard lock(live_mutex);
    for (auto* effect : live_effects) effect->host->request_callback(effect->host);
}
// How many live instances there are, and how many on_main_thread callbacks
// they have received between them.
extern "C" CLAP_EXPORT int blokkily_test_effect_instances() {
    const std::lock_guard lock(live_mutex);
    return static_cast<int>(live_effects.size());
}
extern "C" CLAP_EXPORT int blokkily_test_effect_main_thread_calls() { return main_thread_calls; }

extern "C" CLAP_EXPORT const clap_plugin_entry_t clap_entry{
    CLAP_VERSION, entry_init, entry_deinit, get_factory};
