// The one real-pixel proof of native plugin windows (item 2.6): the VST3
// fixture's editor, which fills 320 x 200 with #C8FF3C, is embedded through
// PluginWindows (the application's own window code) in a Qt window on the
// xcb platform, on a real X server. The window is then grabbed back from the
// server, and its centre pixel must be the editor's colour: the editor really
// reached the screen inside the host's window, at the right size.
//
// features/plugin_windows.feature:
//   Scenario: On a real X11 display the VST3 editor is drawn inside the host window
//
// Skips with 77 where there is no X server to connect to (no DISPLAY, or one
// that cannot be opened): a headless CI without Xvfb cannot run it. Writes
// plugin-window-x11.png beside the other verification screenshots.

#include "plugin_run_loop_qt.hpp"
#include "plugin_windows.hpp"

#include "blokkily/plugins/vst3_instance.hpp"

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#include <dlfcn.h>

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {

// Whether an X server answers at DISPLAY, asked through libX11 directly so
// that Qt is never started on a display it cannot open (it would abort).
bool x_server_reachable() {
    const char* name = std::getenv("DISPLAY");
    if (name == nullptr || *name == '\0') return false;
    void* library = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (library == nullptr) return false;
    using Open = void* (*)(const char*);
    using Close = int (*)(void*);
    auto open = reinterpret_cast<Open>(dlsym(library, "XOpenDisplay"));
    auto close = reinterpret_cast<Close>(dlsym(library, "XCloseDisplay"));
    if (open == nullptr || close == nullptr) return false;
    void* display = open(name);
    if (display == nullptr) return false;
    close(display);
    return true;
}

// The window's contents as the X server has them, children included (the
// editor is a child window of another client, the plugin's own JUCE), read
// with XGetImage the way `import -window` reads them. Qt's own grab of a
// window it drew nothing into would only return its (black) backing.
QImage grab_from_server(unsigned long window) {
    void* library = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
    if (library == nullptr) return {};
    using Display = void;
    using Image = void;
    auto open = reinterpret_cast<Display* (*)(const char*)>(dlsym(library, "XOpenDisplay"));
    auto close = reinterpret_cast<int (*)(Display*)>(dlsym(library, "XCloseDisplay"));
    auto geometry = reinterpret_cast<int (*)(Display*, unsigned long, unsigned long*, int*, int*,
                                             unsigned*, unsigned*, unsigned*, unsigned*)>(
        dlsym(library, "XGetGeometry"));
    auto get_image = reinterpret_cast<Image* (*)(Display*, unsigned long, int, int, unsigned,
                                                 unsigned, unsigned long, int)>(
        dlsym(library, "XGetImage"));
    auto get_pixel = reinterpret_cast<unsigned long (*)(Image*, int, int)>(dlsym(library, "XGetPixel"));
    auto destroy = reinterpret_cast<int (*)(Image*)>(dlsym(library, "XDestroyImage"));
    if (!open || !close || !geometry || !get_image || !get_pixel || !destroy) return {};
    Display* display = open(std::getenv("DISPLAY"));
    if (display == nullptr) return {};
    QImage result;
    unsigned long root = 0;
    int x = 0, y = 0;
    unsigned width = 0, height = 0, border = 0, depth = 0;
    if (geometry(display, window, &root, &x, &y, &width, &height, &border, &depth) != 0 &&
        width > 0 && height > 0) {
        constexpr unsigned long all_planes = ~0UL;
        constexpr int z_pixmap = 2;
        if (Image* image = get_image(display, window, 0, 0, width, height, all_planes, z_pixmap)) {
            result = QImage(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGB32);
            for (unsigned row = 0; row < height; ++row)
                for (unsigned column = 0; column < width; ++column)
                    result.setPixel(static_cast<int>(column), static_cast<int>(row),
                                    0xff000000U | static_cast<unsigned>(get_pixel(
                                                      image, static_cast<int>(column),
                                                      static_cast<int>(row)) & 0xffffffU));
            destroy(image);
        }
    }
    close(display);
    return result;
}

void run_events(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

} // namespace

int main(int argc, char** argv) {
    if (!x_server_reachable()) {
        std::cout << "SKIP: no X11 display to put a plugin window on (DISPLAY="
                  << (std::getenv("DISPLAY") ? std::getenv("DISPLAY") : "") << ")\n";
        return 77;
    }
    qputenv("QT_QPA_PLATFORM", "xcb");
    QGuiApplication app(argc, argv);
    if (QGuiApplication::platformName() != QLatin1String("xcb")) {
        std::cout << "SKIP: Qt could not use the xcb platform\n";
        return 77;
    }
    QtPluginRunLoop run_loop;
    const blokkily::ScopedPluginRunLoop installed(run_loop);

    std::string error;
    auto plugin = blokkily::Vst3PluginInstance::create(BLOKKILY_TEST_VST3_PATH, 0, &error);
    if (!plugin) {
        std::cerr << "FAIL: the VST3 fixture must instantiate: " << error << '\n';
        return 1;
    }
    int status = 0;
    {
        PluginWindows windows;
        const auto where = blokkily::track_instrument(0);
        QString reason;
        if (!windows.open(where, *plugin, QStringLiteral("Blokkily Test VST3 · display check"),
                          &reason)) {
            std::cerr << "FAIL: the editor did not open: " << reason.toStdString() << '\n';
            return 1;
        }
        QWindow* window = windows.window(where);
        if (window == nullptr || windows.placement(where) != PluginWindows::Placement::embedded) {
            std::cerr << "FAIL: on xcb the editor must be embedded in a window of the host\n";
            return 1;
        }
        const qreal ratio = window->devicePixelRatio();
        std::cout << "host window 0x" << std::hex << window->winId() << std::dec << ", logical "
                  << window->width() << "x" << window->height() << " at ratio " << ratio << '\n';

        // Draws come through JUCE's queue, which the run loop pumps, and the
        // X server's expose events; grab until the editor's colour is there.
        QImage grabbed;
        QColor centre;
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < 8000) {
            run_events(100);
            if (!window->isExposed()) continue;
            grabbed = grab_from_server(static_cast<unsigned long>(window->winId()));
            if (grabbed.isNull()) continue;
            centre = grabbed.pixelColor(grabbed.width() / 2, grabbed.height() / 2);
            if (centre == QColor(0xc8, 0xff, 0x3c)) break;
        }
        const QString directory = QStringLiteral(BLOKKILY_TEST_ARTIFACTS);
        QDir{}.mkpath(directory);
        const QString path = directory + QStringLiteral("/plugin-window-x11.png");
        if (!grabbed.isNull()) (void)grabbed.save(path, "PNG");
        std::cout << "grab " << grabbed.width() << "x" << grabbed.height() << ", centre "
                  << centre.name().toStdString() << ", written to " << path.toStdString() << '\n';
        const int tolerance = std::max(2, static_cast<int>(std::ceil(ratio)) + 2);
        const bool sized = !grabbed.isNull() &&
                           std::abs(grabbed.width() - 320) <= tolerance &&
                           std::abs(grabbed.height() - 200) <= tolerance;
        if (centre != QColor(0xc8, 0xff, 0x3c)) {
            std::cerr << "FAIL: the centre of the host window is not the editor's #c8ff3c\n";
            status = 1;
        } else if (!sized) {
            std::cerr << "FAIL: the host window is not the editor's 320x200 physical pixels\n";
            status = 1;
        }
        // Closing the host window closes the editor.
        window->close();
        run_events(100);
        if (windows.isOpen(where) || plugin->editor_open()) {
            std::cerr << "FAIL: closing the window must close the editor\n";
            status = 1;
        }
    }
    plugin.reset();
    std::cout << (status == 0 ? "PASS" : "FAIL")
              << ": the VST3 editor is drawn inside the host's X11 window\n";
    return status;
}
