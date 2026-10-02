#pragma once

// Top-level X11 windows the host makes itself, for an X11-only editor while
// Qt runs on Wayland. Qt cannot have an xcb window beside its Wayland ones, so
// the window is made on a connection of the host's own to the X server
// (XWayland under a Wayland desktop) and the editor is embedded in it exactly
// as it would be in a Qt xcb window.
//
// libX11 is loaded at run time, as the VST3 adapter loads it, and nothing
// here includes Xlib or Qt headers: Xlib's macros (Bool, None, Status) must
// not meet Qt's. Main thread only.

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class X11HostWindows {
public:
    // A connection to the X server named by DISPLAY; nullptr when there is
    // none, libX11 cannot be loaded, or the server does not answer.
    [[nodiscard]] static std::unique_ptr<X11HostWindows> connect();
    ~X11HostWindows();
    X11HostWindows(const X11HostWindows&) = delete;
    X11HostWindows& operator=(const X11HostWindows&) = delete;

    // The connection's descriptor, readable when the server has sent events.
    [[nodiscard]] int descriptor() const;

    // A new, unmapped top-level of `width` x `height` X pixels, titled
    // `title`, that exists on the server by the time this returns (an editor
    // may be parented to it at once). 0 on failure.
    std::uintptr_t create(const std::string& title, std::uint32_t width, std::uint32_t height);
    void destroy(std::uintptr_t window);
    // The window's size, held fixed there: the editor, not the user, decides it.
    void resize(std::uintptr_t window, std::uint32_t width, std::uint32_t height);
    void show(std::uintptr_t window);
    void hide(std::uintptr_t window);

    // Reads what the server has sent and returns the windows the user asked
    // to close (the window manager's WM_DELETE_WINDOW) since the last call.
    std::vector<std::uintptr_t> take_close_requests();

private:
    struct Impl;
    explicit X11HostWindows(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
