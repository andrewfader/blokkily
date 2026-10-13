#include "blokkily/plugins/dynamic_library.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace blokkily::dynamic_library {

std::filesystem::path module_path(const std::filesystem::path& path) {
    std::error_code error;
    const auto executables = path / "Contents" / "MacOS";
    if (!std::filesystem::is_directory(path, error) ||
        !std::filesystem::is_directory(executables, error))
        return path;
    const auto named = executables / path.stem();
    if (std::filesystem::is_regular_file(named, error)) return named;
    std::filesystem::path only;
    for (const auto& entry : std::filesystem::directory_iterator(executables, error)) {
        if (!entry.is_regular_file(error)) continue;
        if (!only.empty()) return path;   // more than one: none is the bundle's
        only = entry.path();
    }
    return only.empty() ? path : only;
}

void* open(const std::filesystem::path& path) {
    const auto file = module_path(path);
#if defined(_WIN32)
    return LoadLibraryW(file.c_str());
#else
    return dlopen(file.c_str(), RTLD_LOCAL | RTLD_NOW);
#endif
}

void* open_loaded(const std::filesystem::path& path) {
    const auto file = module_path(path);
#if defined(_WIN32)
    HMODULE module = nullptr;
    return GetModuleHandleExW(0, file.c_str(), &module) ? module : nullptr;
#else
    return dlopen(file.c_str(), RTLD_NOW | RTLD_NOLOAD);
#endif
}

void close(void* library) {
    if (library == nullptr) return;
#if defined(_WIN32)
    FreeLibrary(static_cast<HMODULE>(library));
#else
    dlclose(library);
#endif
}

void* symbol(void* library, const char* name) {
    if (library == nullptr) return nullptr;
#if defined(_WIN32)
    return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(library), name));
#else
    return dlsym(library, name);
#endif
}

void* loaded_symbol(const std::filesystem::path& path, const char* name) {
    void* library = open_loaded(path);
    void* address = symbol(library, name);
    close(library);
    return address;
}

} // namespace blokkily::dynamic_library
