#include "processor_factory.hpp"
#include "sampler_processor.hpp"

#include "blokkily/effects/builtin.hpp"
#include "blokkily/instruments/soundfont_synth.hpp"
#include "blokkily/plugins/plugin_scan.hpp"
#include "blokkily/plugins/sandbox.hpp"

#include <utility>

namespace blokkily {
namespace {

std::filesystem::path& isolation_host() {
    static std::filesystem::path host;
    return host;
}

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

std::uint64_t& revision() {
    static std::uint64_t changes = 0;
    return changes;
}

const Registration* find(const std::string& format) {
    for (const auto& registration : registry())
        if (registration.format == format) return &registration;
    return nullptr;
}

} // namespace

void register_internal(const std::string& format, ProcessorFactory factory,
                       std::vector<CatalogEntry> entries) {
    ++revision();
    for (auto& registration : registry())
        if (registration.format == format) {
            registration.factory = std::move(factory);
            registration.entries = std::move(entries);
            return;
        }
    registry().push_back({format, std::move(factory), std::move(entries)});
}

void set_plugin_isolation(const std::filesystem::path& host) { isolation_host() = host; }
std::filesystem::path plugin_isolation() { return isolation_host(); }

bool is_internal_format(const std::string& format) { return find(format) != nullptr; }

std::uint64_t internal_catalog_revision() { return revision(); }

std::vector<CatalogEntry> internal_catalog() {
    std::vector<CatalogEntry> entries;
    for (const auto& registration : registry())
        entries.insert(entries.end(), registration.entries.begin(), registration.entries.end());
    return entries;
}

std::unique_ptr<PluginInstance> create_processor(const InstrumentSlot& slot,
                                                 const ProcessorContext& context,
                                                 std::string* error) {
    if (is_plugin_format(slot.format)) {
        // A plugin runs in a helper process of its own when isolation is on
        // (features/plugin_isolation.feature); a crash there costs only it.
        if (const auto host = plugin_isolation(); !host.empty() && sandbox_supported())
            return create_sandboxed(slot, host, error);
        return create_format_processor(slot, error);
    }
    if (const auto* registration = find(slot.format); registration && registration->factory)
        return registration->factory(slot, context, error);
    if (error != nullptr) *error = "unknown instrument format";
    return nullptr;
}

} // namespace blokkily
