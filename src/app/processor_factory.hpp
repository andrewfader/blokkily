#pragma once

// Creating a processor from the slot the song names (plan F-D, C12). Plugin
// formats go to their adapters; the processors the application provides
// itself (the SoundFont player now, the sampler and the built-in effects
// later) are found in an internal registry, so no caller special-cases one.
// Control thread only. Needs no Qt, so headless tests run the same code.

#include "blokkily/model/song.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace blokkily {

class AudioAssetCache;   // plan F-B (item 1.3); may be null until then

// What a processor may need beyond its slot to be created.
struct ProcessorContext {
    // Where the project lives, for slots that store project-relative paths.
    std::filesystem::path project_dir;
    // Decoded audio shared between processors that play files.
    AudioAssetCache* assets = nullptr;
};

// An internal processor a browser can offer without a scan, such as a
// built-in effect. `kind` is what the browser groups it under: "instrument"
// or "effect" (plugin_scan.hpp). The built-in effects are registered from the
// start, under the format "Built-in".
struct CatalogEntry {
    std::string name;
    std::string kind;
    InstrumentSlot slot;
};

using ProcessorFactory = std::function<std::unique_ptr<PluginInstance>(
    const InstrumentSlot&, const ProcessorContext&, std::string* error)>;

// Makes `factory` the creator of every slot whose format is `format`,
// replacing whatever was registered under it, and lists `entries` in
// internal_catalog(). The SoundFont player is registered from the start.
void register_internal(const std::string& format, ProcessorFactory factory,
                       std::vector<CatalogEntry> entries = {});
// Whether `format` is served by the internal registry.
[[nodiscard]] bool is_internal_format(const std::string& format);
// Every entry the internal processors offer, in registration order.
[[nodiscard]] std::vector<CatalogEntry> internal_catalog();

// The processor `slot` names, without its state loaded, or nullptr with the
// reason in `error`.
[[nodiscard]] std::unique_ptr<PluginInstance> create_processor(const InstrumentSlot& slot,
                                                               const ProcessorContext& context,
                                                               std::string* error);

} // namespace blokkily
