#pragma once

// The built-in sampler as the processor factory offers it (plan item 2.3,
// C12/C13). Two catalog entries share one format: "Sampler" opens on an empty
// keyed program and "Drum Sampler" on an empty kit. Which one a slot started
// as is its identifier ("keyed" or "kit"); what it plays afterwards is its
// state, the SamplerProgram blob, so a keyed sampler chopped into slices is
// still the same processor and is never rebuilt for it. Control thread only;
// needs no Qt.

#include "processor_factory.hpp"

#include <memory>
#include <string>
#include <vector>

namespace blokkily {

inline constexpr const char* sampler_format = "Sampler";
inline constexpr const char* sampler_keyed_identifier = "keyed";
inline constexpr const char* sampler_kit_identifier = "kit";

// A SamplerInstrument that decodes through `context.assets` and resolves
// project-relative sample paths against `context.project_dir`, holding the
// empty program of the mode the slot's identifier names. The slot's saved
// state is loaded by the caller, like any other processor's.
[[nodiscard]] std::unique_ptr<PluginInstance> create_sampler(const InstrumentSlot& slot,
                                                             const ProcessorContext& context,
                                                             std::string* error);
// "Sampler" and "Drum Sampler", both of kind "instrument".
[[nodiscard]] std::vector<CatalogEntry> sampler_catalog();

} // namespace blokkily
