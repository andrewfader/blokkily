#pragma once

// The CLAP fixture's editor on macOS and Windows: a real child view of the
// window the host gives it, filled with #C8FF3C (the VST3 fixture's colour),
// so a host's window can be checked by its pixels. Elsewhere the editor
// draws nothing and these do nothing.

#include <cstdint>

namespace blokkily::test_clap_native {

#if defined(__APPLE__) || defined(_WIN32)
// A child of `parent` (an NSView* on macOS, an HWND on Windows) of `width` x
// `height` (points on macOS, pixels on Windows); nullptr when it cannot be
// made.
void* attach(void* parent, std::uint32_t width, std::uint32_t height);
void resize(void* child, std::uint32_t width, std::uint32_t height);
void detach(void* child);
#else
inline void* attach(void*, std::uint32_t, std::uint32_t) { return nullptr; }
inline void resize(void*, std::uint32_t, std::uint32_t) {}
inline void detach(void*) {}
#endif

} // namespace blokkily::test_clap_native
