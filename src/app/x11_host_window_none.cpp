#include "x11_host_window.hpp"

// Where there is no X server to reach (Windows, macOS), there is never a
// connection: connect() reports none, as it does on Linux without DISPLAY,
// and an X11-only editor is refused the same way. Nothing else can be called
// without a connection.

struct X11HostWindows::Impl {};

X11HostWindows::X11HostWindows(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
X11HostWindows::~X11HostWindows() = default;

std::unique_ptr<X11HostWindows> X11HostWindows::connect() { return nullptr; }

int X11HostWindows::descriptor() const { return -1; }

std::uintptr_t X11HostWindows::create(const std::string&, std::uint32_t, std::uint32_t) {
    return 0;
}

void X11HostWindows::destroy(std::uintptr_t) {}
void X11HostWindows::resize(std::uintptr_t, std::uint32_t, std::uint32_t) {}
void X11HostWindows::show(std::uintptr_t) {}
void X11HostWindows::hide(std::uintptr_t) {}

std::vector<std::uintptr_t> X11HostWindows::take_close_requests() { return {}; }
