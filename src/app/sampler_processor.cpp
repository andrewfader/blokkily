#include "sampler_processor.hpp"

#include "blokkily/instruments/sampler.hpp"

namespace blokkily {

std::unique_ptr<PluginInstance> create_sampler(const InstrumentSlot& slot,
                                               const ProcessorContext& context,
                                               std::string* error) {
    auto sampler = std::make_unique<SamplerInstrument>(context.assets);
    sampler->set_base_directory(context.project_dir);
    const auto mode = slot.identifier == sampler_kit_identifier ? SamplerProgram::Mode::kit
                                                                : SamplerProgram::Mode::keyed;
    if (!sampler->set_program(default_sampler(mode), error)) return nullptr;
    return sampler;
}

std::vector<CatalogEntry> sampler_catalog() {
    return {
        {"Sampler", "instrument", {sampler_format, "", sampler_keyed_identifier, {}}},
        {"Drum Sampler", "instrument", {sampler_format, "", sampler_kit_identifier, {}}},
    };
}

} // namespace blokkily
