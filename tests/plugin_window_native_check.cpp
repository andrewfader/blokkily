// The real-pixel proof of native plugin windows on macOS and Windows: a
// fixture's editor is embedded through PluginWindows (the application's own
// window code) in a Qt window on the platform's own windowing (Qt's cocoa or
// windows platform). The platform is then asked what is inside that window,
// and its pixels are read back:
//  - `vst3`: the VST3 fixture's JUCE editor, through the production JUCE host
//    adapter. It fills 320 x 200 with #C8FF3C.
//  - `clap`: the CLAP fixture's editor through the production CLAP adapter,
//    given the window as a Cocoa NSView or a Win32 HWND. It puts a #C8FF3C
//    child view of 320 x 200 inside it (tests/fixtures/test_clap_native.hpp).
// Either way the editor must be a native child of the host window, the host
// window must be the editor's size in logical units, its centre must be the
// editor's colour, and the close request the window's close button makes must
// close the editor.
//
// features/plugin_windows.feature:
//   Scenario: On macOS and Windows a plugin's editor is drawn inside the host window
//
// Writes plugin-window-<platform>-<format>.png beside the other verification
// screenshots.

#include "plugin_run_loop_qt.hpp"
#include "plugin_windows.hpp"
#include "fixtures/test_clap_gui.hpp"
#include "support/native_window_probe.hpp"

#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/dynamic_library.hpp"
#include "blokkily/plugins/vst3_instance.hpp"

#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QTimer>
#include <QWindow>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

namespace {

const QColor editor_colour(0xc8, 0xff, 0x3c);
constexpr int editor_width = 320;
constexpr int editor_height = 200;

void run_events(int milliseconds) {
    QEventLoop loop;
    QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
    loop.exec();
}

// Colour management may move a channel by a step or two on its way to the
// screen; nothing else is that close to the fixtures' colour.
bool is_editor_colour(const QColor& colour) {
    return std::abs(colour.red() - editor_colour.red()) <= 6 &&
           std::abs(colour.green() - editor_colour.green()) <= 6 &&
           std::abs(colour.blue() - editor_colour.blue()) <= 6;
}

bool near(int value, int wanted) { return std::abs(value - wanted) <= 2; }

// The CLAP fixture's record of the window it was given, from the very module
// the adapter loaded.
std::array<long, blokkily::test_clap_gui::report_size> clap_report() {
    std::array<long, blokkily::test_clap_gui::report_size> values{};
    static void* module = blokkily::dynamic_library::open(BLOKKILY_TEST_CLAP_PATH);
    if (module == nullptr) return values;
    auto* report = reinterpret_cast<void (*)(long*)>(
        blokkily::dynamic_library::symbol(module, "blokkily_test_gui_report"));
    if (report != nullptr) report(values.data());
    return values;
}

} // namespace

int main(int argc, char** argv) {
    const bool clap = argc > 1 && std::strcmp(argv[1], "clap") == 0;
    const bool vst3 = argc > 1 && std::strcmp(argv[1], "vst3") == 0;
    if (!clap && !vst3) {
        std::cerr << "usage: blokkily_plugin_window_native_check <vst3|clap>\n";
        return 2;
    }
    QGuiApplication app(argc, argv);
    const QString platform = QGuiApplication::platformName();
    std::cout << "Qt platform " << platform.toStdString() << '\n';
    if (platform != QLatin1String("cocoa") && platform != QLatin1String("windows")) {
        std::cerr << "FAIL: this check runs on the cocoa or windows platform\n";
        return 1;
    }
    QtPluginRunLoop run_loop;
    const blokkily::ScopedPluginRunLoop installed(run_loop);

    std::string error;
    std::unique_ptr<blokkily::PluginInstance> plugin;
    if (vst3) plugin = blokkily::Vst3PluginInstance::create(BLOKKILY_TEST_VST3_PATH, 0, &error);
    else plugin = blokkily::ClapPluginInstance::create(BLOKKILY_TEST_CLAP_PATH,
                                                       "dev.blokkily.test", &error);
    if (!plugin) {
        std::cerr << "FAIL: the " << argv[1] << " fixture must instantiate: " << error << '\n';
        return 1;
    }
    const std::string format = vst3 ? "VST3" : "CLAP";
    int status = 0;
    const auto fail = [&status](const std::string& message) {
        std::cerr << "FAIL: " << message << '\n';
        status = 1;
    };
    {
        PluginWindows windows;
        const auto where = blokkily::track_instrument(0);
        QString reason;
        if (!windows.open(where, *plugin,
                          QString::fromStdString("Blokkily Test " + format + " · native check"),
                          &reason)) {
            std::cerr << "FAIL: the editor did not open: " << reason.toStdString() << '\n';
            return 1;
        }
        QWindow* window = windows.window(where);
        if (window == nullptr || windows.placement(where) != PluginWindows::Placement::embedded) {
            std::cerr << "FAIL: the editor must be embedded in a window of the host\n";
            return 1;
        }
        const WId host = window->winId();
        std::cout << "host window " << reinterpret_cast<void*>(host) << ", logical "
                  << window->width() << "x" << window->height() << " at ratio "
                  << window->devicePixelRatio() << '\n';
        if (clap) {
            const auto report = clap_report();
            const bool given = report[blokkily::test_clap_gui::last_parent_native] == 1 &&
                               report[blokkily::test_clap_gui::last_parent] ==
                                   static_cast<long>(static_cast<std::uintptr_t>(host));
            std::cout << "the CLAP editor was given " << reinterpret_cast<void*>(
                             static_cast<std::uintptr_t>(report[blokkily::test_clap_gui::last_parent]))
                      << (report[blokkily::test_clap_gui::last_parent_native] ? " (native)" : "")
                      << '\n';
            if (!given) fail("the CLAP editor must be given the host window as a native parent");
        }

        // The editor paints through the platform's own event loop, which Qt
        // runs; read until its colour is there.
        std::vector<QSize> inside;
        std::vector<std::pair<QString, QImage>> grabs;
        QString seen_by;
        QImage seen;
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < 10000 && seen_by.isEmpty()) {
            run_events(100);
            if (!window->isExposed()) continue;
            inside = native_probe::children(host);
            grabs = native_probe::grab(host);
            for (const auto& [means, image] : grabs)
                if (!image.isNull() &&
                    is_editor_colour(image.pixelColor(image.width() / 2, image.height() / 2))) {
                    seen_by = means;
                    seen = image;
                    break;
                }
        }
        const QString directory = QStringLiteral(BLOKKILY_TEST_ARTIFACTS);
        QDir{}.mkpath(directory);
        const QString path = directory + QStringLiteral("/plugin-window-%1-%2.png")
                                             .arg(platform, QString::fromStdString(argv[1]));
        for (const auto& [means, image] : grabs) {
            std::cout << "grab by " << means.toStdString() << ": ";
            if (image.isNull()) {
                std::cout << "none\n";
                continue;
            }
            std::cout << image.width() << "x" << image.height() << ", centre "
                      << image.pixelColor(image.width() / 2, image.height() / 2).name().toStdString()
                      << '\n';
        }
        if (seen.isNull())
            for (const auto& grabbed : grabs)
                if (!grabbed.second.isNull()) {
                    seen = grabbed.second;
                    break;
                }
        if (!seen.isNull()) (void)seen.save(path, "PNG");
        std::cout << "written to " << path.toStdString() << '\n';

        std::cout << inside.size() << " native view(s) inside the host window:";
        for (const auto& size : inside) std::cout << ' ' << size.width() << 'x' << size.height();
        std::cout << '\n';
        const bool embedded = std::any_of(inside.begin(), inside.end(), [](const QSize& size) {
            return near(size.width(), editor_width) && near(size.height(), editor_height);
        });
        if (!embedded) fail("the editor's own 320x200 view must be inside the host window");
        if (!near(window->width(), editor_width) || !near(window->height(), editor_height))
            fail("the host window must be the editor's 320x200 logical size");
        if (seen_by.isEmpty()) fail("the centre of the host window is not the editor's #c8ff3c");
        else std::cout << "the editor's colour was read back by " << seen_by.toStdString() << '\n';

        // The window's close button closes the editor.
        if (!native_probe::request_close(host)) fail("the close request could not be made");
        QElapsedTimer closing;
        closing.start();
        while (windows.isOpen(where) && closing.elapsed() < 3000) run_events(50);
        if (windows.isOpen(where) || plugin->editor_open())
            fail("closing the window must close the editor");
    }
    plugin.reset();
    std::cout << (status == 0 ? "PASS" : "FAIL") << ": the " << format
              << " editor is drawn inside the host's " << platform.toStdString() << " window\n";
    return status;
}
