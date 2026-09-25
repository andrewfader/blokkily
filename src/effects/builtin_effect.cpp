#include "builtin_effect.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace blokkily::effects {

namespace {

// The state stream: a magic, a version, the parameter count, then each
// parameter as (int32 id, float64 value), in host byte order.
constexpr std::array<char, 4> state_magic{'B', 'K', 'F', 'X'};
constexpr std::uint32_t state_version = 1;

template <typename Value>
void put(std::vector<std::byte>& bytes, const Value& value) {
    const auto* raw = reinterpret_cast<const std::byte*>(&value);
    bytes.insert(bytes.end(), raw, raw + sizeof value);
}

template <typename Value>
bool take(std::span<const std::byte>& bytes, Value& value) {
    if (bytes.size() < sizeof value) return false;
    std::memcpy(&value, bytes.data(), sizeof value);
    bytes = bytes.subspan(sizeof value);
    return true;
}

} // namespace

BuiltinEffect::BuiltinEffect(std::string identifier, std::span<const ParameterSpec> specs)
    : identifier_(std::move(identifier)),
      specs_(specs),
      base_(std::make_unique<std::atomic<double>[]>(specs.size())),
      modulation_(std::make_unique<double[]>(specs.size())) {
    for (std::size_t id = 0; id < specs_.size(); ++id) {
        base_[id].store(specs_[id].default_value, std::memory_order_relaxed);
        modulation_[id] = 0.0;
    }
}

bool BuiltinEffect::activate(double sample_rate, std::uint32_t min_frames,
                             std::uint32_t max_frames) {
    if (!(sample_rate > 0.0) || min_frames == 0 || max_frames < min_frames) return false;
    sample_rate_ = sample_rate;
    for (std::size_t id = 0; id < specs_.size(); ++id) modulation_[id] = 0.0;
    prepare(sample_rate);
    update();
    dirty_.store(false, std::memory_order_relaxed);
    active_ = true;
    return true;
}

double BuiltinEffect::base(std::size_t id) const noexcept {
    return id < specs_.size() ? base_[id].load(std::memory_order_relaxed) : 0.0;
}

double BuiltinEffect::value(std::size_t id) const noexcept {
    if (id >= specs_.size()) return 0.0;
    return std::clamp(base_[id].load(std::memory_order_relaxed) + modulation_[id],
                      specs_[id].min, specs_[id].max);
}

void BuiltinEffect::apply(const PluginEvent& event) noexcept {
    if (event.key_or_parameter < 0 ||
        static_cast<std::size_t>(event.key_or_parameter) >= specs_.size())
        return;
    const auto id = static_cast<std::size_t>(event.key_or_parameter);
    if (event.type == PluginEvent::Type::parameter_value) {
        base_[id].store(std::clamp(event.value, specs_[id].min, specs_[id].max),
                        std::memory_order_relaxed);
    } else if (event.type == PluginEvent::Type::parameter_modulation) {
        modulation_[id] = event.value;
    } else {
        return; // notes mean nothing to an effect
    }
    dirty_.store(true, std::memory_order_relaxed);
}

void BuiltinEffect::process(StereoBlock audio, std::span<const PluginEvent> events) noexcept {
    // Not yet activated there is nothing sized to render with; the signal
    // passes untouched rather than being lost.
    if (!active_ || audio.left.size() != audio.right.size()) return;
    const std::size_t frames = audio.left.size();
    float* left = audio.left.data();
    float* right = audio.right.data();
    std::size_t cursor = 0;
    const auto render_to = [&](std::size_t end) {
        if (end <= cursor) return;
        if (dirty_.exchange(false, std::memory_order_relaxed)) update();
        render(left + cursor, right + cursor, end - cursor);
        cursor = end;
    };
    // Events arrive in time order; one that claims an earlier sample than the
    // last is applied where the render has got to.
    for (const auto& event : events) {
        render_to(std::min<std::size_t>(event.sample_offset, frames));
        apply(event);
    }
    render_to(frames);
}

void BuiltinEffect::set_transport(const TransportInfo& transport) noexcept {
    if (!(transport.bpm > 0.0)) return;
    if (bpm_.exchange(transport.bpm, std::memory_order_relaxed) != transport.bpm)
        dirty_.store(true, std::memory_order_relaxed);
}

std::vector<ParameterInfo> BuiltinEffect::parameters() const {
    std::vector<ParameterInfo> result;
    result.reserve(specs_.size());
    for (std::size_t id = 0; id < specs_.size(); ++id)
        result.push_back({static_cast<std::int32_t>(id), specs_[id].name, specs_[id].min,
                          specs_[id].max, specs_[id].default_value, true});
    return result;
}

std::vector<std::byte> BuiltinEffect::save_state() {
    std::vector<std::byte> bytes;
    bytes.reserve(12 + specs_.size() * 12);
    put(bytes, state_magic);
    put(bytes, state_version);
    put(bytes, static_cast<std::uint32_t>(specs_.size()));
    for (std::size_t id = 0; id < specs_.size(); ++id) {
        put(bytes, static_cast<std::int32_t>(id));
        put(bytes, base_[id].load(std::memory_order_relaxed));
    }
    return bytes;
}

bool BuiltinEffect::load_state(std::span<const std::byte> bytes) {
    std::array<char, 4> magic{};
    std::uint32_t version = 0;
    std::uint32_t count = 0;
    if (!take(bytes, magic) || magic != state_magic || !take(bytes, version) ||
        version != state_version || !take(bytes, count) || bytes.size() != count * 12ULL)
        return false;
    // Everything is read before anything changes, so a bad stream leaves the
    // effect as it was. A parameter the stream does not mention takes its
    // default; one this build does not know is ignored.
    std::vector<double> values(specs_.size());
    for (std::size_t id = 0; id < specs_.size(); ++id) values[id] = specs_[id].default_value;
    for (std::uint32_t index = 0; index < count; ++index) {
        std::int32_t id = 0;
        double value = 0.0;
        (void)take(bytes, id);
        (void)take(bytes, value);
        if (id < 0 || static_cast<std::size_t>(id) >= specs_.size()) continue;
        if (!(value == value)) return false; // NaN
        const auto& spec = specs_[static_cast<std::size_t>(id)];
        values[static_cast<std::size_t>(id)] = std::clamp(value, spec.min, spec.max);
    }
    for (std::size_t id = 0; id < specs_.size(); ++id)
        base_[id].store(values[id], std::memory_order_relaxed);
    dirty_.store(true, std::memory_order_relaxed);
    return true;
}

} // namespace blokkily::effects
