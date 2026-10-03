#include "blokkily/plugins/dynamic_library.hpp"

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace blokkily::dynamic_library {

void* open(const std::filesystem::path& path) {
#if defined(_WIN32)
    return LoadLibraryW(path.c_str());
#else
    return dlopen(path.c_str(), RTLD_LOCAL | RTLD_NOW);
#endif
}

void* open_loaded(const std::filesystem::path& path) {
#if defined(_WIN32)
    HMODULE module = nullptr;
    return GetModuleHandleExW(0, path.c_str(), &module) ? module : nullptr;
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
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
