#include "test_clap_gui.hpp"

#include <clap/clap.h>
#include <clap/ext/audio-ports.h>
#include <clap/ext/gui.h>
#include <clap/ext/latency.h>
#include <clap/ext/params.h>
#include <clap/ext/posix-fd-support.h>
#include <clap/ext/state.h>
#include <clap/ext/tail.h>
#include <clap/ext/thread-check.h>
#include <clap/ext/timer-support.h>

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <new>
#include <string>

namespace {
void (*process_observer)(void*) = nullptr;
void* observer_context = nullptr;
// The fixture's lifecycle log: how many instances the host created and
// destroyed, so a test can prove an instance was adopted and not reloaded.
std::atomic<long> instances_created{0};
std::atomic<long> instances_destroyed{0};
constexpr const char* features[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT,
                                    CLAP_PLUGIN_FEATURE_SYNTHESIZER, nullptr};
constexpr clap_plugin_descriptor_t descriptor{
    CLAP_VERSION, "dev.blokkily.test", "Blokkily Test Synth", "Blokkily",
    "https://blokkily.invalid", "", "", "0.1.0", "Test fixture", features};

// The parameters the fixture exposes through clap.params. VelocityMode is
// declared here (plan C7); its sound (item 1.6) is described below.
constexpr clap_id level_id = 0;
constexpr clap_id tone_id = 1;
constexpr clap_id velocity_mode_id = 2;
constexpr std::uint32_t parameter_count = 3;

// What the host told the fixture about its threads, for the tests to read.
// -1 = never asked, 0 = false, 1 = true.
struct ThreadReport {
    std::atomic<int> init_main{-1}, init_audio{-1};
    std::atomic<int> process_main{-1}, process_audio{-1};
    std::atomic<int> callback_main{-1}, callback_audio{-1};
};
ThreadReport thread_report;
std::atomic<int> main_thread_calls{0};
std::atomic<std::uint32_t> most_inputs_seen{0};
std::atomic<std::uint32_t> latency_samples{0};
std::atomic<std::uint32_t> tail_samples{0};
// The transport the host handed the last process() call, if any.
std::atomic<double> transport_tempo{0.0};
std::atomic<double> transport_beats{-1.0};
std::atomic<int> transport_bar{-1};

// The fixture answers three questions. At rest it holds a steady level, which
// is what the timing and mixer gates measure. Asked for a tone it becomes a real
// oscillator at the pitch it was told to play, so a retuned note can be proved
// from the audio rather than from the event that asked for it. Asked for its
// velocity mode (parameter 2) it holds every key that is down and sounds
// level × the sum of their velocities, so how hard and how long each voice of
// a chord was struck can be read straight off the rendered plateau.
constexpr int maximum_held = 32;
struct HeldKey {
    int key;
    float velocity;
    // Per-note expression (CLAP_EVENT_NOTE_EXPRESSION, addressed by key),
    // 0 at each note-on as CLAP says.
    float pressure = 0.0F;
    float brightness = 0.0F;
};
struct TestSynth {
    clap_plugin_t plugin;
    const clap_host_t* host = nullptr;
    const clap_host_thread_check_t* thread_check = nullptr;
    const clap_host_params_t* host_params = nullptr;
    const clap_host_latency_t* host_latency = nullptr;
    const clap_host_tail_t* host_tail = nullptr;
    bool sounding = false;
    float level = 0.25F;
    float tone = 0.0F;
    float velocity_mode = 0.0F;
    // CLAP_EVENT_PARAM_MOD amounts, per parameter: a non-destructive offset
    // the host replaces with each new one, heard on top of the value above
    // (which params.value keeps reporting unmodulated).
    float modulation[3]{};
    double sample_rate = 48000.0;
    double phase = 0.0;
    int key = 60;
    double tuning_semitones = 0.0;
    // MIDI poly pressure (0xA0), per key, 0..1: kept as a keyboard's key
    // pressure is, until the key's next message, so a chased value is heard
    // by the next note on that key.
    float poly[128]{};
    // Every key that is down, whatever the mode, so turning velocity mode on
    // between a note-on and its note-off still reads correctly. Fixed size:
    // process() must not allocate.
    HeldKey held[maximum_held]{};
    int held_count = 0;
    // A knob turned in the fixture's "window", waiting for the host to flush.
    std::atomic<bool> turn_pending{false};
    std::atomic<clap_id> turn_id{0};
    std::atomic<double> turn_value{0.0};

    // The editor (clap.gui). It draws nothing; it keeps a timer and a pipe
    // registered with the host while it exists, the way a real editor keeps
    // its animation timer and its display connection.
    const clap_host_gui_t* host_gui = nullptr;
    const clap_host_timer_support_t* host_timers = nullptr;
    const clap_host_posix_fd_support_t* host_fds = nullptr;
    bool gui_created = false;
    bool gui_floating = false;
    std::uint32_t gui_width = 320;
    std::uint32_t gui_height = 200;
    clap_id gui_timer = CLAP_INVALID_ID;
    int gui_pipe[2] = {-1, -1};
    bool gui_fd_registered = false;
};

// What every editor was asked, for blokkily_test_gui_report(), and the order
// the host called things in, for blokkily_test_gui_log().
std::mutex gui_mutex;
std::array<long, blokkily::test_clap_gui::report_size> gui_report{};
std::string gui_log;
void gui_record(const char* entry) {
    const std::lock_guard lock(gui_mutex);
    if (!gui_log.empty()) gui_log += ',';
    gui_log += entry;
}
void gui_count(int field, long delta = 1) {
    const std::lock_guard lock(gui_mutex);
    gui_report[static_cast<std::size_t>(field)] += delta;
}
void gui_set(int field, long value) {
    const std::lock_guard lock(gui_mutex);
    gui_report[static_cast<std::size_t>(field)] = value;
}
void press(TestSynth& synth, int key, float velocity) {
    for (int index = 0; index < synth.held_count; ++index)
        if (synth.held[index].key == key) {
            synth.held[index] = {key, velocity};
            return;
        }
    if (synth.held_count < maximum_held) synth.held[synth.held_count++] = {key, velocity};
}
HeldKey* held_key(TestSynth& synth, int key) {
    for (int index = 0; index < synth.held_count; ++index)
        if (synth.held[index].key == key) return &synth.held[index];
    return nullptr;
}
// How loud a held key sounds, beyond its velocity: pressure (poly pressure
// and per-note pressure alike) raises it by up to double, brightness by up
// to double again. 1 for a key nothing has pressed on, so every level the
// other tests read is untouched.
float expression_gain(const TestSynth& synth, int key) {
    float pressure = key >= 0 && key < 128 ? synth.poly[key] : 0.0F;
    float brightness = 0.0F;
    for (int index = 0; index < synth.held_count; ++index)
        if (synth.held[index].key == key) {
            pressure += synth.held[index].pressure;
            brightness = synth.held[index].brightness;
        }
    return (1.0F + pressure) * (1.0F + brightness);
}
void lift(TestSynth& synth, int key) {
    for (int index = 0; index < synth.held_count; ++index)
        if (synth.held[index].key == key) {
            synth.held[index] = synth.held[--synth.held_count];
            return;
        }
}
float held_velocity(const TestSynth& synth) {
    float sum = 0.0F;
    for (int index = 0; index < synth.held_count; ++index)
        sum += synth.held[index].velocity * expression_gain(synth, synth.held[index].key);
    return sum;
}
TestSynth* self(const clap_plugin_t* plugin) { return static_cast<TestSynth*>(plugin->plugin_data); }

// Every live instance, so the exported hooks can reach them the way the
// plugin's own window would.
std::mutex live_mutex;
std::array<TestSynth*, 256> live{};

template <typename Visit>
void for_each_live(Visit visit) {
    const std::lock_guard lock(live_mutex);
    for (auto* synth : live)
        if (synth != nullptr) visit(*synth);
}

int ask(bool (*question)(const clap_host_t*), const clap_host_t* host) {
    return question(host) ? 1 : 0;
}

float* parameter(TestSynth& synth, clap_id id) {
    switch (id) {
    case level_id: return &synth.level;
    case tone_id: return &synth.tone;
    case velocity_mode_id: return &synth.velocity_mode;
    default: return nullptr;
    }
}

// The last modulation each parameter received, and how many, for a test to
// read the events the host sent rather than only their effect.
std::array<std::atomic<double>, 3> modulation_seen{};
std::atomic<long> modulations_received{0};
float effective(const TestSynth& synth, clap_id id) {
    const auto* self_value = parameter(const_cast<TestSynth&>(synth), id);
    return self_value == nullptr ? 0.0F : *self_value + synth.modulation[id];
}

bool plugin_init(const clap_plugin_t* plugin) {
    auto* synth = self(plugin);
    const auto* host = synth->host;
    synth->thread_check = static_cast<const clap_host_thread_check_t*>(
        host->get_extension(host, CLAP_EXT_THREAD_CHECK));
    synth->host_params =
        static_cast<const clap_host_params_t*>(host->get_extension(host, CLAP_EXT_PARAMS));
    synth->host_latency =
        static_cast<const clap_host_latency_t*>(host->get_extension(host, CLAP_EXT_LATENCY));
    synth->host_tail =
        static_cast<const clap_host_tail_t*>(host->get_extension(host, CLAP_EXT_TAIL));
    synth->host_gui = static_cast<const clap_host_gui_t*>(host->get_extension(host, CLAP_EXT_GUI));
    synth->host_timers = static_cast<const clap_host_timer_support_t*>(
        host->get_extension(host, CLAP_EXT_TIMER_SUPPORT));
    synth->host_fds = static_cast<const clap_host_posix_fd_support_t*>(
        host->get_extension(host, CLAP_EXT_POSIX_FD_SUPPORT));
    if (synth->thread_check) {
        thread_report.init_main = ask(synth->thread_check->is_main_thread, host);
        thread_report.init_audio = ask(synth->thread_check->is_audio_thread, host);
    }
    const std::lock_guard lock(live_mutex);
    for (auto*& slot : live)
        if (slot == nullptr) {
            slot = synth;
            break;
        }
    return true;
}
void plugin_destroy(const clap_plugin_t* plugin) {
    auto* synth = self(plugin);
    {
        const std::lock_guard lock(live_mutex);
        for (auto*& slot : live)
            if (slot == synth) slot = nullptr;
    }
    instances_destroyed.fetch_add(1);
    // A host must destroy the editor first; one that did not is on record.
    gui_record(synth->gui_created ? "plugin-destroy-with-editor" : "plugin-destroy");
    delete synth;
}
bool plugin_activate(const clap_plugin_t* plugin, double sample_rate, std::uint32_t,
                     std::uint32_t) {
    self(plugin)->sample_rate = sample_rate;
    return true;
}
void plugin_deactivate(const clap_plugin_t*) {}
bool plugin_start(const clap_plugin_t*) { return true; }
void plugin_stop(const clap_plugin_t*) {}
void plugin_reset(const clap_plugin_t* plugin) {
    self(plugin)->sounding = false;
    self(plugin)->held_count = 0;
    std::fill(std::begin(self(plugin)->poly), std::end(self(plugin)->poly), 0.0F);
}

void apply_parameter(TestSynth& synth, const clap_event_header_t* header) {
    if (header->type == CLAP_EVENT_PARAM_VALUE) {
        const auto* change = reinterpret_cast<const clap_event_param_value_t*>(header);
        if (auto* value = parameter(synth, change->param_id))
            *value = static_cast<float>(change->value);
    } else if (header->type == CLAP_EVENT_PARAM_MOD) {
        const auto* modulation = reinterpret_cast<const clap_event_param_mod_t*>(header);
        if (parameter(synth, modulation->param_id) != nullptr) {
            synth.modulation[modulation->param_id] = static_cast<float>(modulation->amount);
            modulation_seen[modulation->param_id] = modulation->amount;
            modulations_received.fetch_add(1);
        }
    }
}

// A pending knob turn becomes one gesture on the host's out_events, and the
// fixture's own value, before anything is rendered.
void emit_turn(TestSynth& synth, const clap_output_events_t* out) {
    if (!synth.turn_pending.exchange(false)) return;
    const clap_id id = synth.turn_id;
    const double value = synth.turn_value;
    if (auto* target = parameter(synth, id)) *target = static_cast<float>(value);
    const clap_event_param_gesture_t begin{
        {sizeof(clap_event_param_gesture_t), 0, CLAP_CORE_EVENT_SPACE_ID,
         CLAP_EVENT_PARAM_GESTURE_BEGIN, 0},
        id};
    const clap_event_param_value_t change{
        {sizeof(clap_event_param_value_t), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0},
        id, nullptr, -1, -1, -1, -1, value};
    const clap_event_param_gesture_t end{
        {sizeof(clap_event_param_gesture_t), 0, CLAP_CORE_EVENT_SPACE_ID,
         CLAP_EVENT_PARAM_GESTURE_END, 0},
        id};
    out->try_push(out, &begin.header);
    out->try_push(out, &change.header);
    out->try_push(out, &end.header);
}

std::array<std::uint8_t, 3> last_midi_seen{};
// The note expressions the host sent, oldest first, for a test to read the
// events themselves (id, key, value, sample) beside their effect.
constexpr int expression_log_size = 4096;
struct ExpressionSeen {
    int id;
    int key;
    double value;
    std::uint32_t time;
};
std::array<ExpressionSeen, expression_log_size> expression_log{};
std::atomic<int> expressions_seen{0};
void record_expression(const clap_event_note_expression_t& expression, std::uint32_t time) {
    const int at = expressions_seen.load(std::memory_order_relaxed);
    if (at >= expression_log_size) return;
    expression_log[static_cast<std::size_t>(at)] = {expression.expression_id, expression.key,
                                                    expression.value, time};
    expressions_seen.store(at + 1, std::memory_order_release);
}
void record_midi(const std::uint8_t* data) {
    if (data != nullptr) {
        last_midi_seen[0] = data[0];
        last_midi_seen[1] = data[1];
        last_midi_seen[2] = data[2];
    }
}

clap_process_status plugin_process(const clap_plugin_t* plugin, const clap_process_t* process) {
    if (process_observer) process_observer(observer_context);
    auto* synth = self(plugin);
    if (synth->thread_check) {
        thread_report.process_main = ask(synth->thread_check->is_main_thread, synth->host);
        thread_report.process_audio = ask(synth->thread_check->is_audio_thread, synth->host);
    }
    // An instrument has no audio input; a host that wires one anyway is seen.
    if (process->audio_inputs_count > most_inputs_seen) most_inputs_seen = process->audio_inputs_count;
    if (process->audio_outputs_count < 1 || process->audio_outputs[0].channel_count != 2)
        return CLAP_PROCESS_ERROR;
    if (process->transport != nullptr) {
        transport_tempo = process->transport->tempo;
        transport_beats = static_cast<double>(process->transport->song_pos_beats) /
                          static_cast<double>(CLAP_BEATTIME_FACTOR);
        transport_bar = process->transport->bar_number;
    }
    emit_turn(*synth, process->out_events);
    auto** channels = process->audio_outputs[0].data32;
    std::uint32_t cursor = 0;
    const auto render_to = [&](std::uint32_t end) {
        const float level = effective(*synth, level_id);
        for (; cursor < end; ++cursor) {
            if (effective(*synth, velocity_mode_id) >= 0.5F) {
                channels[0][cursor] = channels[1][cursor] = level * held_velocity(*synth);
                continue;
            }
            if (!synth->sounding) {
                channels[0][cursor] = channels[1][cursor] = 0.0F;
                continue;
            }
            const float pressed = level * expression_gain(*synth, synth->key);
            if (effective(*synth, tone_id) < 0.5F) {
                channels[0][cursor] = channels[1][cursor] = pressed;
                continue;
            }
            // Equal temperament from the key, moved by whatever tuning the host
            // asked for: a nineteen-tone scale arrives as a fraction of a
            // semitone and must be heard as one.
            const double frequency = 440.0 * std::pow(
                2.0, (synth->key - 69 + synth->tuning_semitones) / 12.0);
            const auto sample = static_cast<float>(pressed * std::sin(synth->phase));
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
            press(*synth, note->key, static_cast<float>(note->velocity));
        } else if (header->type == CLAP_EVENT_NOTE_OFF) {
            const auto* note = reinterpret_cast<const clap_event_note_t*>(header);
            synth->sounding = false;
            lift(*synth, note->key);
        } else if (header->type == CLAP_EVENT_NOTE_EXPRESSION) {
            const auto* expression = reinterpret_cast<const clap_event_note_expression_t*>(header);
            record_expression(*expression, header->time);
            // Addressed by key (or to every key, -1), as the host sends it.
            const bool ours = expression->key < 0 || expression->key == synth->key;
            if (expression->expression_id == CLAP_NOTE_EXPRESSION_TUNING && ours)
                synth->tuning_semitones = expression->value;
            for (int index = 0; index < synth->held_count; ++index) {
                auto& held = synth->held[index];
                if (expression->key >= 0 && held.key != expression->key) continue;
                if (expression->expression_id == CLAP_NOTE_EXPRESSION_PRESSURE)
                    held.pressure = static_cast<float>(expression->value);
                else if (expression->expression_id == CLAP_NOTE_EXPRESSION_BRIGHTNESS)
                    held.brightness = static_cast<float>(expression->value);
            }
        } else if (header->type == CLAP_EVENT_MIDI) {
            const auto* midi = reinterpret_cast<const clap_event_midi_t*>(header);
            record_midi(midi->data);
            const auto status = static_cast<std::uint8_t>(midi->data[0] & 0xF0U);
            if (status == 0xE0) {
                const int bend = static_cast<int>(midi->data[1]) | (static_cast<int>(midi->data[2]) << 7);
                synth->tuning_semitones = (bend - 8192) / 8192.0 * 2.0;
            } else if (status == 0xA0) {
                synth->poly[midi->data[1] & 0x7F] = static_cast<float>(midi->data[2] & 0x7F) / 127.0F;
            }
        } else {
            apply_parameter(*synth, header);
        }
    }
    render_to(process->frames_count);
    // The aux output (port 1) carries the main signal at minus half, so a
    // host that routes it can be told apart from one that mixes it into the
    // main output: main alone is +level, main plus aux +level/2.
    if (process->audio_outputs_count > 1) {
        const auto& aux = process->audio_outputs[1];
        for (std::uint32_t channel = 0; channel < aux.channel_count; ++channel)
            for (std::uint32_t frame = 0; frame < process->frames_count; ++frame)
                aux.data32[channel][frame] = -0.5F * channels[0][frame];
    }
    return CLAP_PROCESS_CONTINUE;
}

// --- clap.params -------------------------------------------------------------
std::uint32_t params_count(const clap_plugin_t*) { return parameter_count; }
bool params_info(const clap_plugin_t*, std::uint32_t index, clap_param_info_t* info) {
    if (index >= parameter_count) return false;
    *info = {};
    info->id = index;
    info->flags = CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
    info->min_value = 0.0;
    info->max_value = 1.0;
    const char* names[] = {"Level", "Tone", "VelocityMode"};
    std::snprintf(info->name, sizeof info->name, "%s", names[index]);
    info->default_value = index == level_id ? 0.25 : 0.0;
    if (index != level_id) info->flags |= CLAP_PARAM_IS_STEPPED;
    return true;
}
bool params_value(const clap_plugin_t* plugin, clap_id id, double* out) {
    const auto* value = parameter(*self(plugin), id);
    if (value == nullptr) return false;
    *out = *value;
    return true;
}
bool params_to_text(const clap_plugin_t*, clap_id id, double value, char* text,
                    std::uint32_t size) {
    if (id >= parameter_count) return false;
    std::snprintf(text, size, "%.2f", value);
    return true;
}
bool params_from_text(const clap_plugin_t*, clap_id id, const char* text, double* value) {
    if (id >= parameter_count) return false;
    char* end = nullptr;
    *value = std::strtod(text, &end);
    return end != text;
}
void params_flush(const clap_plugin_t* plugin, const clap_input_events_t* in,
                  const clap_output_events_t* out) {
    auto* synth = self(plugin);
    for (std::uint32_t index = 0; index < in->size(in); ++index)
        apply_parameter(*synth, in->get(in, index));
    emit_turn(*synth, out);
}
constexpr clap_plugin_params_t params_extension{params_count, params_info, params_value,
                                                params_to_text, params_from_text, params_flush};

// --- clap.state ----------------------------------------------------------------
// Version 2 holds a tag and every parameter. Version 1, the original 4-byte
// stream holding only the level, still loads.
constexpr std::array<char, 4> state_tag{'B', 'K', 'S', '2'};
struct StateV2 {
    std::array<char, 4> tag;
    float level, tone, velocity_mode;
};

bool save_state(const clap_plugin_t* plugin, const clap_ostream_t* stream) {
    const auto* synth = self(plugin);
    const StateV2 state{state_tag, synth->level, synth->tone, synth->velocity_mode};
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
    auto* synth = self(plugin);
    std::array<char, sizeof(StateV2) + 1> bytes{};
    std::size_t read = 0;
    while (read < bytes.size()) {
        const auto count = stream->read(stream, bytes.data() + read, bytes.size() - read);
        if (count < 0) return false;
        if (count == 0) break;
        read += static_cast<std::size_t>(count);
    }
    if (read == sizeof(float)) {
        std::memcpy(&synth->level, bytes.data(), sizeof(float));
        return true;
    }
    StateV2 state{};
    if (read != sizeof state) return false;
    std::memcpy(&state, bytes.data(), sizeof state);
    if (state.tag != state_tag) return false;
    synth->level = state.level;
    synth->tone = state.tone;
    synth->velocity_mode = state.velocity_mode;
    return true;
}
constexpr clap_plugin_state_t state_extension{save_state, load_state};

// --- clap.audio-ports: an instrument, nothing in, a main stereo output and ---
// an auxiliary stereo output (a multi-output instrument's second pair).
std::uint32_t ports_count(const clap_plugin_t*, bool is_input) { return is_input ? 0 : 2; }
bool ports_get(const clap_plugin_t*, std::uint32_t index, bool is_input,
               clap_audio_port_info_t* info) {
    if (is_input || index > 1) return false;
    *info = {};
    info->id = index;
    std::snprintf(info->name, sizeof info->name, "%s", index == 0 ? "Output" : "Aux");
    info->flags = index == 0 ? CLAP_AUDIO_PORT_IS_MAIN : 0U;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}
constexpr clap_plugin_audio_ports_t audio_ports_extension{ports_count, ports_get};

std::uint32_t latency_get(const clap_plugin_t*) { return latency_samples; }
constexpr clap_plugin_latency_t latency_extension{latency_get};
std::uint32_t tail_get(const clap_plugin_t*) { return tail_samples; }
constexpr clap_plugin_tail_t tail_extension{tail_get};

// --- clap.gui ------------------------------------------------------------------
// An X11 or Wayland editor, embedded or floating. It records what the host
// asks and the window it was given rather than drawing, so it works without a
// display.
namespace gui_report_field = blokkily::test_clap_gui;

bool gui_is_api_supported(const clap_plugin_t*, const char* api, bool) {
    return api != nullptr && (std::strcmp(api, CLAP_WINDOW_API_X11) == 0 ||
                              std::strcmp(api, CLAP_WINDOW_API_WAYLAND) == 0);
}
bool gui_get_preferred_api(const clap_plugin_t*, const char** api, bool* is_floating) {
    *api = CLAP_WINDOW_API_X11;
    *is_floating = false;
    return true;
}
bool gui_create(const clap_plugin_t* plugin, const char* api, bool is_floating) {
    auto* synth = self(plugin);
    if (synth->gui_created || !gui_is_api_supported(plugin, api, is_floating)) return false;
    synth->gui_created = true;
    synth->gui_floating = is_floating;
    synth->gui_width = 320;
    synth->gui_height = 200;
    gui_record(is_floating ? "create-floating" : "create-embedded");
    gui_count(gui_report_field::creates);
    gui_count(gui_report_field::open_editors);
    gui_set(gui_report_field::last_floating, is_floating ? 1 : 0);
    gui_set(gui_report_field::width, synth->gui_width);
    gui_set(gui_report_field::height, synth->gui_height);
    // An editor's animation timer and its connection to the display.
    if (synth->host_timers != nullptr &&
        synth->host_timers->register_timer(synth->host, 20, &synth->gui_timer))
        gui_count(gui_report_field::timers_registered);
    else
        synth->gui_timer = CLAP_INVALID_ID;
    if (synth->host_fds != nullptr && pipe2(synth->gui_pipe, O_NONBLOCK | O_CLOEXEC) == 0) {
        synth->gui_fd_registered =
            synth->host_fds->register_fd(synth->host, synth->gui_pipe[0], CLAP_POSIX_FD_READ);
        if (synth->gui_fd_registered) gui_count(gui_report_field::fds_registered);
    }
    return true;
}
void gui_destroy(const clap_plugin_t* plugin) {
    auto* synth = self(plugin);
    if (!synth->gui_created) return;
    if (synth->gui_timer != CLAP_INVALID_ID) {
        if (synth->host_timers->unregister_timer(synth->host, synth->gui_timer))
            gui_count(gui_report_field::timers_registered, -1);
        synth->gui_timer = CLAP_INVALID_ID;
    }
    if (synth->gui_fd_registered) {
        if (synth->host_fds->unregister_fd(synth->host, synth->gui_pipe[0]))
            gui_count(gui_report_field::fds_registered, -1);
        synth->gui_fd_registered = false;
    }
    for (int& end : synth->gui_pipe)
        if (end >= 0) {
            close(end);
            end = -1;
        }
    synth->gui_created = false;
    gui_record("destroy");
    gui_count(gui_report_field::destroys);
    gui_count(gui_report_field::open_editors, -1);
}
bool gui_set_scale(const clap_plugin_t*, double scale) {
    gui_record("scale");
    gui_set(gui_report_field::scale_percent, std::lround(scale * 100.0));
    return true;
}
bool gui_get_size(const clap_plugin_t* plugin, std::uint32_t* width, std::uint32_t* height) {
    const auto* synth = self(plugin);
    if (!synth->gui_created) return false;
    gui_record("size");
    *width = synth->gui_width;
    *height = synth->gui_height;
    return true;
}
bool gui_can_resize(const clap_plugin_t*) { return true; }
bool gui_get_resize_hints(const clap_plugin_t*, clap_gui_resize_hints_t* hints) {
    *hints = {true, true, false, 0, 0};
    return true;
}
bool gui_adjust_size(const clap_plugin_t*, std::uint32_t* width, std::uint32_t* height) {
    *width = std::max<std::uint32_t>(*width, 100);
    *height = std::max<std::uint32_t>(*height, 60);
    return true;
}
bool gui_set_size(const clap_plugin_t* plugin, std::uint32_t width, std::uint32_t height) {
    auto* synth = self(plugin);
    synth->gui_width = width;
    synth->gui_height = height;
    gui_record("set-size");
    gui_set(gui_report_field::width, width);
    gui_set(gui_report_field::height, height);
    return true;
}
bool gui_set_parent(const clap_plugin_t* plugin, const clap_window_t* window) {
    const auto* synth = self(plugin);
    if (!synth->gui_created || synth->gui_floating || window == nullptr || window->api == nullptr ||
        !gui_is_api_supported(plugin, window->api, false))
        return false;
    gui_record("parent");
    gui_count(gui_report_field::set_parents);
    const bool wayland = std::strcmp(window->api, CLAP_WINDOW_API_WAYLAND) == 0;
    gui_set(gui_report_field::last_parent, static_cast<long>(
        wayland ? reinterpret_cast<std::uintptr_t>(window->ptr)
                : static_cast<std::uintptr_t>(window->x11)));
    gui_set(gui_report_field::last_parent_wayland, wayland ? 1 : 0);
    return true;
}
bool gui_set_transient(const clap_plugin_t*, const clap_window_t*) { return true; }
void gui_suggest_title(const clap_plugin_t*, const char*) { gui_record("title"); }
bool gui_show(const clap_plugin_t* plugin) {
    if (!self(plugin)->gui_created) return false;
    gui_record("show");
    gui_count(gui_report_field::shows);
    return true;
}
bool gui_hide(const clap_plugin_t* plugin) {
    if (!self(plugin)->gui_created) return false;
    gui_record("hide");
    gui_count(gui_report_field::hides);
    return true;
}
constexpr clap_plugin_gui_t gui_extension{
    gui_is_api_supported, gui_get_preferred_api, gui_create,      gui_destroy,
    gui_set_scale,        gui_get_size,          gui_can_resize,  gui_get_resize_hints,
    gui_adjust_size,      gui_set_size,          gui_set_parent,  gui_set_transient,
    gui_suggest_title,    gui_show,              gui_hide};

// --- clap.timer-support and clap.posix-fd-support ---------------------------------
void on_timer(const clap_plugin_t* plugin, clap_id id) {
    if (self(plugin)->gui_timer == id) gui_count(gui_report_field::timer_ticks);
}
constexpr clap_plugin_timer_support_t timer_extension{on_timer};
void on_fd(const clap_plugin_t* plugin, int fd, clap_posix_fd_flags_t flags) {
    auto* synth = self(plugin);
    if (fd != synth->gui_pipe[0] || (flags & CLAP_POSIX_FD_READ) == 0) return;
    char byte = 0;
    bool read_any = false;
    while (read(fd, &byte, 1) == 1) read_any = true;
    if (read_any) gui_count(gui_report_field::fd_events);
}
constexpr clap_plugin_posix_fd_support_t fd_extension{on_fd};

const void* plugin_extension(const clap_plugin_t*, const char* id) {
    if (std::strcmp(id, CLAP_EXT_GUI) == 0) return &gui_extension;
    if (std::strcmp(id, CLAP_EXT_TIMER_SUPPORT) == 0) return &timer_extension;
    if (std::strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT) == 0) return &fd_extension;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0) return &state_extension;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0) return &params_extension;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0) return &audio_ports_extension;
    if (std::strcmp(id, CLAP_EXT_LATENCY) == 0) return &latency_extension;
    if (std::strcmp(id, CLAP_EXT_TAIL) == 0) return &tail_extension;
    return nullptr;
}
void plugin_main_thread(const clap_plugin_t* plugin) {
    auto* synth = self(plugin);
    if (synth->thread_check) {
        thread_report.callback_main = ask(synth->thread_check->is_main_thread, synth->host);
        thread_report.callback_audio = ask(synth->thread_check->is_audio_thread, synth->host);
    }
    ++main_thread_calls;
}

std::uint32_t count(const clap_plugin_factory_t*) { return 1; }
const clap_plugin_descriptor_t* describe(const clap_plugin_factory_t*, std::uint32_t index) {
    return index == 0 ? &descriptor : nullptr;
}
const clap_plugin_t* create(const clap_plugin_factory_t*, const clap_host_t* host, const char* id) {
    if (id == nullptr || std::strcmp(id, descriptor.id) != 0) return nullptr;
    auto* synth = new (std::nothrow) TestSynth{};
    if (synth == nullptr) return nullptr;
    synth->host = host;
    instances_created.fetch_add(1);
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

// A knob turned in the plugin's own window, on every live instance: the value
// is held until the host flushes (request_flush), then the next process() or
// flush() sends GESTURE_BEGIN, PARAM_VALUE and GESTURE_END through out_events.
extern "C" CLAP_EXPORT void blokkily_test_gui_turn(clap_id id, double value) {
    for_each_live([&](TestSynth& synth) {
        synth.turn_id = id;
        synth.turn_value = value;
        synth.turn_pending = true;
        if (synth.host_params) synth.host_params->request_flush(synth.host);
    });
}

// Asks each live instance's host for an on_main_thread callback. Safe from any
// thread, as clap_host.request_callback is.
extern "C" CLAP_EXPORT void blokkily_test_request_callback() {
    for_each_live([](TestSynth& synth) { synth.host->request_callback(synth.host); });
}

// How many on_main_thread callbacks the fixture has received.
extern "C" CLAP_EXPORT int blokkily_test_main_thread_calls() { return main_thread_calls; }

// What thread-check answered: in init, in the last process(), and in the last
// on_main_thread callback, each as {is_main_thread, is_audio_thread}.
extern "C" CLAP_EXPORT void blokkily_test_thread_report(int* answers) {
    answers[0] = thread_report.init_main;
    answers[1] = thread_report.init_audio;
    answers[2] = thread_report.process_main;
    answers[3] = thread_report.process_audio;
    answers[4] = thread_report.callback_main;
    answers[5] = thread_report.callback_audio;
}

// The most audio inputs any process() call was handed.
extern "C" CLAP_EXPORT std::uint32_t blokkily_test_inputs_seen() { return most_inputs_seen; }

// The note expressions every instance was sent since the last clear: up to
// `capacity` of them into the four arrays; returns how many were sent.
extern "C" CLAP_EXPORT int blokkily_test_expressions(int* ids, int* keys, double* values,
                                                     std::uint32_t* times, int capacity) {
    const int seen = expressions_seen.load(std::memory_order_acquire);
    for (int index = 0; index < seen && index < capacity; ++index) {
        const auto& entry = expression_log[static_cast<std::size_t>(index)];
        ids[index] = entry.id;
        keys[index] = entry.key;
        values[index] = entry.value;
        times[index] = entry.time;
    }
    return seen;
}
extern "C" CLAP_EXPORT void blokkily_test_clear_expressions() {
    expressions_seen.store(0, std::memory_order_release);
}

// Changes the latency or tail the plugin reports and tells each host, as a
// plugin does when a setting changes them. Call on the main thread.
extern "C" CLAP_EXPORT void blokkily_test_set_latency(std::uint32_t samples) {
    latency_samples = samples;
    for_each_live([](TestSynth& synth) {
        if (synth.host_latency) synth.host_latency->changed(synth.host);
    });
}
extern "C" CLAP_EXPORT void blokkily_test_set_tail(std::uint32_t samples) {
    tail_samples = samples;
    for_each_live([](TestSynth& synth) {
        if (synth.host_tail) synth.host_tail->changed(synth.host);
    });
}

// The transport of the last process() call: {tempo, beats, bar}.
extern "C" CLAP_EXPORT void blokkily_test_transport(double* answers) {
    answers[0] = transport_tempo;
    answers[1] = transport_beats;
    answers[2] = transport_bar;
}

// The lifecycle log, read by tests that must tell an adopted instance from a
// reloaded one.
extern "C" CLAP_EXPORT void blokkily_test_instance_counts(long* created, long* destroyed) {
    if (created != nullptr) *created = instances_created.load();
    if (destroyed != nullptr) *destroyed = instances_destroyed.load();
}

// --- The editor, driven the way its window would be ------------------------------

// Everything the editors were asked, indexed by blokkily::test_clap_gui::Report.
extern "C" CLAP_EXPORT void blokkily_test_gui_report(long* values) {
    const std::lock_guard lock(gui_mutex);
    std::copy(gui_report.begin(), gui_report.end(), values);
}

// The calls the host made, in order, comma separated ("create-embedded,scale,
// size,parent,show,..."), NUL-terminated into `text`; returns the full length.
extern "C" CLAP_EXPORT std::size_t blokkily_test_gui_log(char* text, std::size_t size) {
    const std::lock_guard lock(gui_mutex);
    if (text != nullptr && size > 0) {
        const auto count = std::min(size - 1, gui_log.size());
        std::memcpy(text, gui_log.data(), count);
        text[count] = '\0';
    }
    return gui_log.size();
}
extern "C" CLAP_EXPORT void blokkily_test_gui_clear_log() {
    const std::lock_guard lock(gui_mutex);
    gui_log.clear();
}

// Something arrives on each open editor's display connection: a byte on the
// pipe the host is watching for it.
extern "C" CLAP_EXPORT void blokkily_test_gui_poke() {
    for_each_live([](TestSynth& synth) {
        if (synth.gui_created && synth.gui_pipe[1] >= 0) {
            const char byte = 1;
            (void)!write(synth.gui_pipe[1], &byte, 1);
        }
    });
}

// Each open editor asks the host for a new size, as one does when a panel of
// it is expanded.
extern "C" CLAP_EXPORT void blokkily_test_gui_request_resize(std::uint32_t width,
                                                             std::uint32_t height) {
    for_each_live([&](TestSynth& synth) {
        if (synth.gui_created && synth.host_gui != nullptr)
            (void)synth.host_gui->request_resize(synth.host, width, height);
    });
}

// Each open editor goes away by itself (its connection to the display was
// lost), which the host must acknowledge by destroying it.
extern "C" CLAP_EXPORT void blokkily_test_gui_request_close() {
    for_each_live([](TestSynth& synth) {
        if (synth.gui_created && synth.host_gui != nullptr) synth.host_gui->closed(synth.host, true);
    });
}

// The last CLAP_EVENT_PARAM_MOD amount each parameter received (Level, Tone,
// VelocityMode), and how many modulation events arrived in all.
extern "C" CLAP_EXPORT long blokkily_test_modulations(double* amounts) {
    for (std::size_t index = 0; index < modulation_seen.size(); ++index)
        amounts[index] = modulation_seen[index];
    return modulations_received;
}

extern "C" CLAP_EXPORT void blokkily_test_last_midi_bytes(std::uint8_t* out) {
    if (out != nullptr) {
        out[0] = last_midi_seen[0];
        out[1] = last_midi_seen[1];
        out[2] = last_midi_seen[2];
    }
}
