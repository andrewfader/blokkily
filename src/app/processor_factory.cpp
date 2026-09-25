#include "processor_factory.hpp"
#include "sampler_processor.hpp"

#include "blokkily/effects/builtin.hpp"
#include "blokkily/instruments/soundfont_synth.hpp"
#include "blokkily/plugins/plugin_scan.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <utility>

namespace blokkily {
namespace {

struct Registration {
    std::string format;
    ProcessorFactory factory;
    std::vector<CatalogEntry> entries;
};

std::unique_ptr<PluginInstance> create_soundfont(const InstrumentSlot& slot,
                                                 const ProcessorContext&, std::string* error) {
    auto synth = std::make_unique<SoundFontSynth>();
    if (synth->load(slot.path)) return synth;
    if (error != nullptr) *error = "could not load SoundFont";
    return nullptr;
}

// Blokkily's own effects (item 2.4): the slot's identifier names which one.
std::unique_ptr<PluginInstance> create_builtin(const InstrumentSlot& slot,
                                               const ProcessorContext&, std::string* error) {
    auto effect = create_builtin_effect(slot.identifier);
    if (!effect && error != nullptr) *error = "unknown built-in effect";
    return effect;
}

std::vector<CatalogEntry> builtin_entries() {
    std::vector<CatalogEntry> entries;
    for (const auto& effect : builtin_effects())
        entries.push_back({effect.name, std::string(effect_kind),
                           {std::string(builtin_effect_format), {}, effect.identifier, {}}});
    return entries;
}

std::vector<Registration>& registry() {
    // Built on first use, with what the application always provides.
    static std::vector<Registration> registrations{
        {"SoundFont", &create_soundfont, {}},
        {sampler_format, &create_sampler, sampler_catalog()},
        {std::string(builtin_effect_format), &create_builtin, builtin_entries()},
    };
    return registrations;
}

const Registration* find(const std::string& format) {
    for (const auto& registration : registry())
        if (registration.format == format) return &registration;
    return nullptr;
}

} // namespace

void register_internal(const std::string& format, ProcessorFactory factory,
                       std::vector<CatalogEntry> entries) {
    for (auto& registration : registry())
        if (registration.format == format) {
            registration.factory = std::move(factory);
            registration.entries = std::move(entries);
            return;
        }
    registry().push_back({format, std::move(factory), std::move(entries)});
}

bool is_internal_format(const std::string& format) { return find(format) != nullptr; }

std::vector<CatalogEntry> internal_catalog() {
    std::vector<CatalogEntry> entries;
    for (const auto& registration : registry())
        entries.insert(entries.end(), registration.entries.begin(), registration.entries.end());
    return entries;
}

std::unique_ptr<PluginInstance> create_processor(const InstrumentSlot& slot,
                                                 const ProcessorContext& context,
                                                 std::string* error) {
    if (slot.format == "CLAP") return ClapPluginInstance::create(slot.path, slot.identifier, error);
    if (slot.format == "VST3") {
        std::size_t index = 0;
        const auto found = Vst3PluginInstance::scan(slot.path);
        for (std::size_t candidate = 0; candidate < found.size(); ++candidate)
            if (found[candidate].identifier == slot.identifier) {
                index = candidate;
                break;
            }
        return Vst3PluginInstance::create(slot.path, index, error);
    }
    if (const auto* registration = find(slot.format); registration && registration->factory)
        return registration->factory(slot, context, error);
    if (error != nullptr) *error = "unknown instrument format";
    return nullptr;
}

} // namespace blokkily
