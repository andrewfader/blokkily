#pragma once

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

struct PluginEvent {
    enum class Type { note_on, note_off, parameter_value, parameter_modulation };
    Type type;
    std::uint32_t sample_offset;
    std::int32_t key_or_parameter;
    double value;
    // How far from the twelve-tone key the note is actually tuned. Every format
    // speaks semitones, so a microtonal pitch travels as the nearest key plus
    // this offset and each adapter says it in its own dialect.
    double cents = 0.0;
};

// A parameter moved by the plugin itself — a knob turned in its own window —
// reported back to the host (plan F-C). Declared here by item 1.1 for the
// engine's edit ring; item 1.4 owns this header and its adapters fill it.
struct ParameterEdit {
    enum class Kind : std::uint8_t { begin, value, end } kind = Kind::value;
    std::int32_t parameter = 0;
    double value = 0.0;
    std::uint32_t sample_offset = 0;
};

class PluginInstance {
public:
    virtual ~PluginInstance() = default;
    virtual bool activate(double sample_rate, std::uint32_t min_frames,
                          std::uint32_t max_frames) = 0;
    virtual void process(StereoBlock audio, std::span<const PluginEvent> events) noexcept = 0;
    virtual std::vector<std::byte> save_state() = 0;
    virtual bool load_state(std::span<const std::byte> state) = 0;
    virtual std::string format() const = 0;
    // Whether load_state() may be called while the instance processes, so a
    // state change reaches it without rebuilding the graph (plan F-C).
    virtual bool accepts_state_while_running() const noexcept { return false; }
    // Edits the plugin made during the last process(), oldest first. Called on
    // the audio thread straight after process(); must not allocate or block.
    virtual std::size_t take_parameter_edits(std::span<ParameterEdit>) noexcept { return 0; }
};

} // namespace blokkily

