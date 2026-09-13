#include <clap/clap.h>
#include <clap/ext/state.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>

namespace {
void (*process_observer)(void*) = nullptr;
void* observer_context = nullptr;
constexpr const char* features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT,
                                    CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr};
constexpr clap_plugin_descriptor_t descriptor{
    CLAP_VERSION, "dev.blokkily.test", "Blokkily Test Synth", "Blokkily",
    "https://blokkily.invalid", "", "", "0.1.0", "Test fixture", features};

// The fixture answers two questions. At rest it holds a steady level, which is
// what the timing and mixer gates measure. Asked for a tone it becomes a real
// oscillator at the pitch it was told to play, so a retuned note can be proved
// from the audio rather than from the event that asked for it.
struct TestSynth {
    clap_plugin_t plugin;
    bool sounding = false;
    float level = 0.25F;
    float tone = 0.0F;
    double sample_rate = 48000.0;
    double phase = 0.0;
    int key = 60;
    double tuning_semitones = 0.0;
};
TestSynth* self(const clap_plugin_t* plugin) { return static_cast<TestSynth*>(plugin->plugin_data); }
bool plugin_init(const clap_plugin_t*) { return true; }
void plugin_destroy(const clap_plugin_t* plugin) { delete self(plugin); }
bool plugin_activate(const clap_plugin_t* plugin, double sample_rate, std::uint32_t,
                     std::uint32_t) {
    self(plugin)->sample_rate = sample_rate;
    return true;
}
void plugin_deactivate(const clap_plugin_t*) {}
bool plugin_start(const clap_plugin_t*) { return true; }
void plugin_stop(const clap_plugin_t*) {}
void plugin_reset(const clap_plugin_t* plugin) { self(plugin)->sounding = false; }

clap_process_status plugin_process(const clap_plugin_t* plugin, const clap_process_t* process) {
    if (process_observer) process_observer(observer_context);
    auto* synth = self(plugin);
    if (process->audio_outputs_count != 1 || process->audio_outputs[0].channel_count != 2)
        return CLAP_PROCESS_ERROR;
    auto** channels = process->audio_outputs[0].data32;
    std::uint32_t cursor = 0;
    const auto render_to = [&](std::uint32_t end) {
        for (; cursor < end; ++cursor) {
            if (!synth->sounding) {
                channels[0][cursor] = channels[1][cursor] = 0.0F;
                continue;
            }
            if (synth->tone < 0.5F) {
                channels[0][cursor] = channels[1][cursor] = synth->level;
                continue;
            }
            // Equal temperament from the key, moved by whatever tuning the host
            // asked for: a nineteen-tone scale arrives as a fraction of a
            // semitone and must be heard as one.
            const double frequency = 440.0 * std::pow(
                2.0, (synth->key - 69 + synth->tuning_semitones) / 12.0);
            const auto sample = static_cast<float>(synth->level * std::sin(synth->phase));
            channels[0][cursor] = channels[1][cursor] = sample;
            synth->phase += 6.283185307179586 * frequency / synth->sample_rate;
            if (synth->phase > 6.283185307179586) synth->phase -= 6.283185307179586;
        }
    };
    const auto event_count = process->in_events->size(process->in_events);
    for (std::uint32_t index = 0; index < event_count; ++index) {
        const auto* header = process->in_events->get(process->in_events, index);
        render_to(std::min(header->time, process->frames_count));
        if (header->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;
        if (header->type == CLAP_EVENT_NOTE_ON) {
            const auto* note = reinterpret_cast<const clap_event_note_t*>(header);
            synth->sounding = true;
            synth->key = note->key;
            // Each note starts from a known tuning, so an untuned note after a
            // retuned one is not still bent.
            synth->tuning_semitones = 0.0;
            synth->phase = 0.0;
        } else if (header->type == CLAP_EVENT_NOTE_OFF) {
            synth->sounding = false;
        } else if (header->type == CLAP_EVENT_NOTE_EXPRESSION) {
            const auto* expression = reinterpret_cast<const clap_event_note_expression_t*>(header);
            if (expression->expression_id == CLAP_NOTE_EXPRESSION_TUNING)
                synth->tuning_semitones = expression->value;
        } else if (header->type == CLAP_EVENT_PARAM_VALUE) {
            const auto* parameter = reinterpret_cast<const clap_event_param_value_t*>(header);
            if (parameter->param_id == 1) synth->tone = static_cast<float>(parameter->value);
            else synth->level = static_cast<float>(parameter->value);
        } else if (header->type == CLAP_EVENT_PARAM_MOD) {
            const auto* modulation = reinterpret_cast<const clap_event_param_mod_t*>(header);
            if (modulation->param_id == 1) synth->tone += static_cast<float>(modulation->amount);
            else synth->level += static_cast<float>(modulation->amount);
        }
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

// Verification observes the real plugin processing boundary while the host
// exports. It does not replace the instrument or its official CLAP entry point.
extern "C" CLAP_EXPORT void blokkily_test_observe_process(void (*observer)(void*), void* context) {
    process_observer = observer;
    observer_context = context;
}
