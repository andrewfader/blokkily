#pragma once

// Shared libraries by path, the same way on every platform: dlopen() and
// dlsym() where there are such things, LoadLibrary() and GetProcAddress() on
// Windows. Every handle that open() or open_loaded() returns is released with
// close().

#include <filesystem>

namespace blokkily::dynamic_library {

// The library at `path`, loaded if it is not already; nullptr on failure.
[[nodiscard]] void* open(const std::filesystem::path& path);
// The library at `path` only if the process has already loaded it; this
// never loads it. nullptr when it is not loaded.
[[nodiscard]] void* open_loaded(const std::filesystem::path& path);
void close(void* library);
[[nodiscard]] void* symbol(void* library, const char* name);

// The address of `name` in the library at `path`, which must already be
// loaded; nullptr when it is not, or has no such symbol. The library stays
// exactly as loaded as it was.
[[nodiscard]] void* loaded_symbol(const std::filesystem::path& path, const char* name);

} // namespace blokkily::dynamic_library
