#include "x11_host_window.hpp"

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <dlfcn.h>

#include <cstdlib>
#include <type_traits>

namespace {

// The Xlib entry points used here, bound from libX11 at run time.
struct Xlib {
    decltype(&::XOpenDisplay) open_display = nullptr;
    decltype(&::XCloseDisplay) close_display = nullptr;
    decltype(&::XCreateSimpleWindow) create_window = nullptr;
    decltype(&::XDestroyWindow) destroy_window = nullptr;
    decltype(&::XMapRaised) map_raised = nullptr;
    decltype(&::XUnmapWindow) unmap_window = nullptr;
    decltype(&::XResizeWindow) resize_window = nullptr;
    decltype(&::XInternAtom) intern_atom = nullptr;
    decltype(&::XSetWMProtocols) set_wm_protocols = nullptr;
    decltype(&::XChangeProperty) change_property = nullptr;
    decltype(&::XStoreName) store_name = nullptr;
    decltype(&::XSetClassHint) set_class_hint = nullptr;
    decltype(&::XSetWMNormalHints) set_normal_hints = nullptr;
    decltype(&::XSelectInput) select_input = nullptr;
    decltype(&::XFlush) flush = nullptr;
    decltype(&::XSync) sync = nullptr;
    decltype(&::XPending) pending = nullptr;
    decltype(&::XNextEvent) next_event = nullptr;
    decltype(&::XConnectionNumber) connection_number = nullptr;
    decltype(&::XDefaultRootWindow) default_root = nullptr;
    bool complete = false;

    static const Xlib& get() {
        static const Xlib xlib = [] {
            Xlib loaded;
            void* library = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
            if (library == nullptr) return loaded;
            bool all = true;
            auto bind = [&](auto& function, const char* name) {
                function = reinterpret_cast<std::remove_reference_t<decltype(function)>>(
                    dlsym(library, name));
                all = all && function != nullptr;
            };
            bind(loaded.open_display, "XOpenDisplay");
            bind(loaded.close_display, "XCloseDisplay");
            bind(loaded.create_window, "XCreateSimpleWindow");
            bind(loaded.destroy_window, "XDestroyWindow");
            bind(loaded.map_raised, "XMapRaised");
            bind(loaded.unmap_window, "XUnmapWindow");
            bind(loaded.resize_window, "XResizeWindow");
            bind(loaded.intern_atom, "XInternAtom");
            bind(loaded.set_wm_protocols, "XSetWMProtocols");
            bind(loaded.change_property, "XChangeProperty");
            bind(loaded.store_name, "XStoreName");
            bind(loaded.set_class_hint, "XSetClassHint");
            bind(loaded.set_normal_hints, "XSetWMNormalHints");
            bind(loaded.select_input, "XSelectInput");
            bind(loaded.flush, "XFlush");
            bind(loaded.sync, "XSync");
            bind(loaded.pending, "XPending");
            bind(loaded.next_event, "XNextEvent");
            bind(loaded.connection_number, "XConnectionNumber");
            bind(loaded.default_root, "XDefaultRootWindow");
            loaded.complete = all;
            return loaded;
        }();
        return xlib;
    }
};

} // namespace

struct X11HostWindows::Impl {
    const Xlib& x = Xlib::get();
    Display* display = nullptr;
    Atom wm_protocols = 0;
    Atom wm_delete = 0;
    Atom net_wm_name = 0;
    Atom utf8_string = 0;

    ~Impl() {
        if (display != nullptr) (void)x.close_display(display);
    }
    void hold_size(Window window, std::uint32_t width, std::uint32_t height) const {
        XSizeHints hints{};
        hints.flags = PMinSize | PMaxSize;
        hints.min_width = hints.max_width = static_cast<int>(width);
        hints.min_height = hints.max_height = static_cast<int>(height);
        x.set_normal_hints(display, window, &hints);
    }
};

X11HostWindows::X11HostWindows(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
X11HostWindows::~X11HostWindows() = default;

std::unique_ptr<X11HostWindows> X11HostWindows::connect() {
    // An empty DISPLAY would make Xlib guess ":0", which may be another
    // user's server.
    const char* name = std::getenv("DISPLAY");
    if (name == nullptr || *name == '\0') return nullptr;
    auto impl = std::make_unique<Impl>();
    if (!impl->x.complete) return nullptr;
    impl->display = impl->x.open_display(name);
    if (impl->display == nullptr) return nullptr;
    auto& x = impl->x;
    impl->wm_protocols = x.intern_atom(impl->display, "WM_PROTOCOLS", False);
    impl->wm_delete = x.intern_atom(impl->display, "WM_DELETE_WINDOW", False);
    impl->net_wm_name = x.intern_atom(impl->display, "_NET_WM_NAME", False);
    impl->utf8_string = x.intern_atom(impl->display, "UTF8_STRING", False);
    return std::unique_ptr<X11HostWindows>(new X11HostWindows(std::move(impl)));
}

int X11HostWindows::descriptor() const { return impl_->x.connection_number(impl_->display); }

std::uintptr_t X11HostWindows::create(const std::string& title, std::uint32_t width,
                                      std::uint32_t height) {
    auto& x = impl_->x;
    auto* display = impl_->display;
    const Window window = x.create_window(display, x.default_root(display), 0, 0,
                                          width > 0 ? width : 1, height > 0 ? height : 1, 0, 0, 0);
    if (window == 0) return 0;
    x.select_input(display, window, StructureNotifyMask);
    Atom protocols[] = {impl_->wm_delete};
    x.set_wm_protocols(display, window, protocols, 1);
    // The legacy name for old window managers, the UTF-8 one for the rest.
    std::string legacy = title;
    x.store_name(display, window, legacy.data());
    x.change_property(display, window, impl_->net_wm_name, impl_->utf8_string, 8, PropModeReplace,
                      reinterpret_cast<const unsigned char*>(title.data()),
                      static_cast<int>(title.size()));
    char instance_name[] = "blokkily";
    char class_name[] = "blokkily";
    XClassHint class_hint{instance_name, class_name};
    x.set_class_hint(display, window, &class_hint);
    impl_->hold_size(window, width, height);
    // The plugin parents its editor through a connection of its own, so the
    // window must reach the server first.
    x.sync(display, False);
    return static_cast<std::uintptr_t>(window);
}

void X11HostWindows::destroy(std::uintptr_t window) {
    if (window == 0) return;
    impl_->x.destroy_window(impl_->display, static_cast<Window>(window));
    impl_->x.flush(impl_->display);
}

void X11HostWindows::resize(std::uintptr_t window, std::uint32_t width, std::uint32_t height) {
    if (window == 0 || width == 0 || height == 0) return;
    impl_->hold_size(static_cast<Window>(window), width, height);
    impl_->x.resize_window(impl_->display, static_cast<Window>(window), width, height);
    impl_->x.flush(impl_->display);
}

void X11HostWindows::show(std::uintptr_t window) {
    if (window == 0) return;
    impl_->x.map_raised(impl_->display, static_cast<Window>(window));
    impl_->x.flush(impl_->display);
}

void X11HostWindows::hide(std::uintptr_t window) {
    if (window == 0) return;
    impl_->x.unmap_window(impl_->display, static_cast<Window>(window));
    impl_->x.flush(impl_->display);
}

std::vector<std::uintptr_t> X11HostWindows::take_close_requests() {
    auto& x = impl_->x;
    std::vector<std::uintptr_t> requests;
    while (x.pending(impl_->display) > 0) {
        XEvent event;
        x.next_event(impl_->display, &event);
        if (event.type == ClientMessage && event.xclient.message_type == impl_->wm_protocols &&
            static_cast<Atom>(event.xclient.data.l[0]) == impl_->wm_delete)
            requests.push_back(static_cast<std::uintptr_t>(event.xclient.window));
    }
    return requests;
}
