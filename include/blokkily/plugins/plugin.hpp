#pragma once

// The plugin boundary (plan F-C). Every processor the engine runs, whatever
// its format, is a PluginInstance; format details stay inside the adapters.
// This header is frozen after item 1.4: later items use it and may add to
// it (wave 5.2 added the sidechain input and the auxiliary outputs), they do
// not reshape it.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace blokkily {

struct StereoBlock {
    std::span<float> left;
    std::span<float> right;
};

// The dimensions of a note_expression event: MPE's three, which CLAP calls
// tuning, brightness and pressure.
namespace note_dimension {
inline constexpr std::uint8_t pitch = 1;    // semitones from the note's tuned pitch
inline constexpr std::uint8_t timbre = 2;   // 0..1 (MPE's CC 74)
inline constexpr std::uint8_t pressure = 3; // 0..1
} // namespace note_dimension

struct PluginEvent {
    // note_expression (wave 4.1, MPE) moves one playing note: `key_or_parameter`
    // is its key, `cents` its retune (so pitch can be said in full), `value`
    // the expression's value and `expression` its dimension (note_dimension).
    enum class Type { note_on, note_off, parameter_value, parameter_modulation, midi_raw,
                      note_expression };
    Type type;
    std::uint32_t sample_offset;
    std::int32_t key_or_parameter;
    double value;
    // How far from the twelve-tone key the note is actually tuned. Every format
    // speaks semitones, so a microtonal pitch travels as the nearest key plus
    // this offset and each adapter says it in its own dialect.
    double cents = 0.0;
    // A note_expression's dimension. On a note_on, 1 says per-note expression
    // may follow (an MPE note): an adapter that carries expression on MIDI
    // channels gives it a channel of its own. 0 otherwise.
    std::uint8_t expression = 0;
};

// --- Native editor windows (implemented by item 2.6) -------------------------
enum class WindowApi { x11, wayland, win32, cocoa };
struct NativeParent {
    WindowApi api;
    std::uintptr_t handle;
    double scale = 1.0;
};
struct EditorSize {
    std::uint32_t width = 0, height = 0;
    bool resizable = false;
};
// What an open editor asks of the window that hosts it. Called on the main
// thread.
class EditorHost {
public:
    virtual ~EditorHost() = default;
    virtual void request_resize(std::uint32_t width, std::uint32_t height) = 0;
    virtual void request_show() = 0;
    virtual void request_hide() = 0;
    virtual void closed() = 0;
};

// The audio a processor takes in and gives out. An instrument has no input;
// an effect takes the track's signal on its main input (1 = mono, 2 =
// stereo). `sidechain_inputs` is the channel count of an auxiliary input the
// processor declares (a CLAP input port that is not the main one, a VST3 aux
// input bus): what set_sidechain() feeds, 0 when there is none.
// `aux_outputs` counts the outputs beyond the main one (CLAP output ports that
// are not the main one, VST3 aux output buses): set_aux_output() names where
// output 1..aux_outputs goes.
struct PluginPorts {
    std::uint32_t audio_inputs = 0;
    bool note_input = true;
    std::uint32_t sidechain_inputs = 0;
    std::uint32_t aux_outputs = 0;
};

// One parameter as the plugin describes it. `id` is what PluginEvent's
// key_or_parameter and ParameterEdit's `parameter` carry: the CLAP param id,
// or the parameter's index for VST3. Values are in the plugin's own range.
struct ParameterInfo {
    std::int32_t id;
    std::string name;
    double min, max, default_value;
    bool automatable;
};

// A parameter moved by the plugin itself (its own window, a knob it turned),
// reported back to the host: one gesture is begin, any number of values, end.
// `sample_offset` places a value inside the block it came out of (always 0
// for edits that did not come from process()).
struct ParameterEdit {
    enum class Kind : std::uint8_t { begin, value, end } kind;
    std::int32_t parameter;
    double value;
    std::uint32_t sample_offset;
};

// Where the song is, handed to the plugin before a block (tempo-synced
// effects read it). `beat` counts quarter notes from the start of the song.
struct TransportInfo {
    double bpm, beat;
    std::int32_t bar;
    std::int16_t numerator, denominator;
    bool playing;
};

class PluginInstance {
public:
    virtual ~PluginInstance() = default;
    virtual bool activate(double sample_rate, std::uint32_t min_frames,
                          std::uint32_t max_frames) = 0;
    // Processes one block IN PLACE, on the audio thread: on entry `audio`
    // holds the processor's input (the track signal for an effect, silence for
    // an instrument), on return its output. Never allocates, locks or blocks.
    virtual void process(StereoBlock audio, std::span<const PluginEvent> events) noexcept = 0;
    virtual std::vector<std::byte> save_state() = 0;
    virtual bool load_state(std::span<const std::byte> state) = 0;
    virtual std::string format() const = 0;

    // The audio this processor takes in (main thread; stable once created).
    virtual PluginPorts ports() const { return {}; }
    // The delay the processor adds, in samples, as of its last activation or
    // latency_changed() poll. Safe on any thread.
    virtual std::uint32_t latency_samples() const noexcept { return 0; }
    // How long it keeps sounding after its input stops; UINT64_MAX = forever.
    virtual std::uint64_t tail_samples() const noexcept { return 0; }
    // Control-thread poll: true once after the plugin announced a new latency,
    // which latency_samples() then reports.
    virtual bool latency_changed() noexcept { return false; }
    // Forgets the signal the processor holds - voices, delay lines, filter
    // memories, a reverb's tail - keeping its parameters and state, so the
    // next block starts from silence as it would straight after activate().
    // Called while process() is not running, on the thread that calls
    // process() next (a bounce's, with the device stopped). CLAP maps it to
    // clap_plugin.reset(); VST3 to JUCE's reset. The default holds nothing.
    virtual void reset() {}
    // Whether load_state may be called while the audio thread is processing.
    virtual bool accepts_state_while_running() const noexcept { return false; }
    // The parameters the plugin exposes (main thread).
    virtual std::vector<ParameterInfo> parameters() const { return {}; }
    // AUDIO thread, straight after process(): moves the plugin's own
    // parameter edits into `out`, oldest first, and returns how many. Edits
    // that do not fit stay queued for the next call.
    virtual std::size_t take_parameter_edits(std::span<ParameterEdit> out) noexcept {
        (void)out;
        return 0;
    }
    // AUDIO thread, before process(): the transport the next block plays in.
    virtual void set_transport(const TransportInfo& transport) noexcept { (void)transport; }
    // AUDIO thread, before process(): sidechain key input audio for this block,
    // exactly as long as the block the next process() is handed, or empty
    // spans for no key (the sidechain input then hears silence).
    virtual void set_sidechain(StereoBlock sidechain) noexcept { (void)sidechain; }
    // AUDIO thread, before process(): where auxiliary output `output` (1 is
    // the first output after the main one) is written by the next process()
    // call, exactly as long as its block; it applies to that call only. An
    // output given nowhere is rendered and discarded, never mixed into the
    // main output. A mono output is written to both sides.
    virtual void set_aux_output(std::uint32_t output, StereoBlock destination) noexcept {
        (void)output;
        (void)destination;
    }
    // Main thread, regularly: services what the plugin asked of the main
    // thread (callbacks, flushes, rescans).
    virtual void idle() {}

    // Native editor windows (item 2.6).
    virtual bool has_editor() const { return false; }
    virtual bool supports_editor(WindowApi api, bool floating) const {
        (void)api;
        (void)floating;
        return false;
    }
    virtual bool open_editor(const NativeParent* parent, EditorHost& host, EditorSize* size,
                             std::string* error) {
        (void)parent;
        (void)host;
        (void)size;
        (void)error;
        return false;
    }
    virtual bool resize_editor(std::uint32_t& width, std::uint32_t& height) {
        (void)width;
        (void)height;
        return false;
    }
    virtual void set_editor_scale(double scale) { (void)scale; }
    virtual void close_editor() {}
    virtual bool editor_open() const { return false; }
};

} // namespace blokkily
