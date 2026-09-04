#pragma once

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
};

} // namespace blokkily

