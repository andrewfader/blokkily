#include "processor_factory.hpp"

#include "blokkily/instruments/soundfont_synth.hpp"
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

std::vector<Registration>& registry() {
    // Built on first use, with what the application always provides.
    static std::vector<Registration> registrations{
        {"SoundFont", &create_soundfont, {}},
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
