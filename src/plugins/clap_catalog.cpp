#include "blokkily/plugins/clap_catalog.hpp"
#include "blokkily/plugins/dynamic_library.hpp"

#include <clap/clap.h>

#include <cstdlib>
#include <memory>
#include <system_error>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace blokkily {
namespace {

std::string safe(const char* value) { return value == nullptr ? std::string{} : value; }

// A macOS bundle: a directory holding its executable in Contents/MacOS.
bool is_bundle(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_directory(path / "Contents" / "MacOS", error);
}

class DynamicLibrary {
public:
    // A macOS bundle is loaded by the executable inside it; the plugin is
    // still told the bundle's path, as CLAP says.
    explicit DynamicLibrary(const std::filesystem::path& path) {
        const auto file = dynamic_library::module_path(path);
#if defined(_WIN32)
        handle_ = LoadLibraryW(file.c_str());
#else
        handle_ = dlopen(file.c_str(), RTLD_LOCAL | RTLD_NOW);
#endif
    }
    ~DynamicLibrary() {
        if (handle_ == nullptr) return;
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
    }
    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    [[nodiscard]] const clap_plugin_entry_t* entry() const {
#if defined(_WIN32)
        return reinterpret_cast<const clap_plugin_entry_t*>(
            GetProcAddress(static_cast<HMODULE>(handle_), "clap_entry"));
#else
        return reinterpret_cast<const clap_plugin_entry_t*>(dlsym(handle_, "clap_entry"));
#endif
    }
    [[nodiscard]] bool valid() const noexcept { return handle_ != nullptr; }
    [[nodiscard]] std::string error() const {
#if defined(_WIN32)
        return "could not load dynamic library (Windows error " +
               std::to_string(GetLastError()) + ")";
#else
        const auto* message = dlerror();
        return message == nullptr ? "could not load dynamic library" : message;
#endif
    }

private:
#if defined(_WIN32)
    void* handle_ = nullptr;
#else
    void* handle_ = nullptr;
#endif
};

void append(ClapScanResult& destination, ClapScanResult source) {
    destination.plugins.insert(destination.plugins.end(),
        std::make_move_iterator(source.plugins.begin()),
        std::make_move_iterator(source.plugins.end()));
    destination.failures.insert(destination.failures.end(),
        std::make_move_iterator(source.failures.begin()),
        std::make_move_iterator(source.failures.end()));
}

} // namespace

ClapScanResult ClapCatalog::scan_file(const std::filesystem::path& file) const {
    ClapScanResult result;
    DynamicLibrary library(file);
    if (!library.valid()) {
        result.failures.push_back({file, library.error()});
        return result;
    }
    const auto* entry = library.entry();
    if (entry == nullptr || !clap_version_is_compatible(entry->clap_version)) {
        result.failures.push_back({file, "missing or incompatible clap_entry"});
        return result;
    }
    const auto path = file.string();
    if (!entry->init(path.c_str())) {
        result.failures.push_back({file, "CLAP entry initialization failed"});
        return result;
    }
    struct Deinit {
        const clap_plugin_entry_t* entry;
        ~Deinit() { entry->deinit(); }
    } deinit{entry};

    const auto* factory = static_cast<const clap_plugin_factory_t*>(
        entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    if (factory == nullptr) {
        result.failures.push_back({file, "plugin factory is unavailable"});
        return result;
    }
    const auto count = factory->get_plugin_count(factory);
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto* descriptor = factory->get_plugin_descriptor(factory, index);
        if (descriptor == nullptr || descriptor->id == nullptr || descriptor->name == nullptr) {
            result.failures.push_back({file, "factory returned an invalid descriptor"});
            continue;
        }
        ClapDescriptor value{file, safe(descriptor->id), safe(descriptor->name),
                             safe(descriptor->vendor), safe(descriptor->version), {}};
        if (descriptor->features != nullptr) {
            for (auto feature = descriptor->features; *feature != nullptr; ++feature) {
                value.features.emplace_back(*feature);
            }
        }
        result.plugins.push_back(std::move(value));
    }
    return result;
}

ClapScanResult ClapCatalog::scan_paths(
    const std::vector<std::filesystem::path>& roots) const {
    ClapScanResult result;
    for (const auto& root : roots) {
        std::error_code error;
        if (root.extension() == ".clap" &&
            (std::filesystem::is_regular_file(root, error) || is_bundle(root))) {
            append(result, scan_file(root));
            continue;
        }
        if (!std::filesystem::exists(root, error)) continue;
        for (std::filesystem::recursive_directory_iterator it(
                 root, std::filesystem::directory_options::skip_permission_denied, error), end;
             it != end; it.increment(error)) {
            if (error) { error.clear(); continue; }
            if (it->path().extension() != ".clap") continue;
            if (it->is_regular_file(error)) {
                append(result, scan_file(it->path()));
            } else if (is_bundle(it->path())) {
                // A macOS bundle is one plugin, not a tree to search.
                append(result, scan_file(it->path()));
                it.disable_recursion_pending();
            }
        }
    }
    return result;
}

std::vector<std::filesystem::path> ClapCatalog::system_paths() {
    std::vector<std::filesystem::path> paths;
#if defined(_WIN32)
    if (const auto* common = std::getenv("COMMONPROGRAMFILES"))
        paths.emplace_back(std::filesystem::path(common) / "CLAP");
#elif defined(__APPLE__)
    if (const auto* home = std::getenv("HOME"))
        paths.emplace_back(std::filesystem::path(home) / "Library/Audio/Plug-Ins/CLAP");
    paths.emplace_back("/Library/Audio/Plug-Ins/CLAP");
#else
    if (const auto* home = std::getenv("HOME"))
        paths.emplace_back(std::filesystem::path(home) / ".clap");
    paths.emplace_back("/usr/lib/clap");
    paths.emplace_back("/usr/local/lib/clap");
#endif
    return paths;
}

} // namespace blokkily
