#include "blokkily/effects/builtin.hpp"

#include "builtin_effect.hpp"

namespace blokkily {

namespace {
struct Entry {
    const char* identifier;
    const char* name;
    std::unique_ptr<PluginInstance> (*make)();
};
constexpr Entry entries[] = {
    {"eq3", "EQ Three", effects::make_eq3},
    {"delay", "Delay", effects::make_delay},
    {"reverb", "Reverb", effects::make_reverb},
    {"compressor", "Compressor", effects::make_compressor},
};
} // namespace

std::vector<BuiltinEffectInfo> builtin_effects() {
    std::vector<BuiltinEffectInfo> result;
    for (const auto& entry : entries) result.push_back({entry.identifier, entry.name});
    return result;
}

std::unique_ptr<PluginInstance> create_builtin_effect(std::string_view identifier) {
    for (const auto& entry : entries)
        if (identifier == entry.identifier) return entry.make();
    return nullptr;
}

} // namespace blokkily
