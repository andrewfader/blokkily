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
//   Scenario: A real third-party plugin plays and draws its editor on macOS and Windows (CI)
//
// Writes plugin-window-<platform>-<format>.png beside the other verification
// screenshots.

#include "plugin_run_loop_qt.hpp"
#include "plugin_windows.hpp"
#include "fixtures/test_clap_gui.hpp"
#include "support/native_window_probe.hpp"

#include "blokkily/plugins/clap_catalog.hpp"
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
#include <cmath>
#include <span>
#include <vector>
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

// `third-party <CLAP|VST3> <path>`: a plugin nobody wrote for this suite,
// installed from its own release, rather than a fixture. It must load, play a
// note, and open its editor inside the host window at a real size, drawing
// something other than one flat colour, and its window's close button must
// close it. Run by CI with a downloaded plugin; never by ctest, which may
// not depend on the network or on installed plugins.
int third_party(const std::string& format, const std::string& path) {
    const QString platform = QGuiApplication::platformName();
    QtPluginRunLoop run_loop;
    const blokkily::ScopedPluginRunLoop installed(run_loop);
    std::string error;
    std::unique_ptr<blokkily::PluginInstance> plugin;
    if (format == "VST3") {
        plugin = blokkily::Vst3PluginInstance::create(path, 0, &error);
    } else if (format == "CLAP") {
        const auto found = blokkily::ClapCatalog{}.scan_file(path);
        if (found.plugins.empty()) {
            std::cerr << "FAIL: the CLAP at " << path << " describes no plugin\n";
            return 1;
        }
        std::cout << "CLAP plugin " << found.plugins.front().id << " ("
                  << found.plugins.front().name << ")\n";
        plugin = blokkily::ClapPluginInstance::create(path, found.plugins.front().id, &error);
    }
    if (!plugin) {
        std::cerr << "FAIL: the " << format << " at " << path << " must instantiate: " << error << '\n';
        return 1;
    }
    int status = 0;
    const auto fail = [&status](const std::string& message) {
        std::cerr << "FAIL: " << message << '\n';
        status = 1;
    };
    // It plays: a held note, a few blocks, a peak above silence.
    if (!plugin->activate(48000.0, 1, 512)) fail("the plugin must activate");
    {
        std::vector<float> left(512), right(512);
        float peak = 0.0F;
        const blokkily::PluginEvent note{blokkily::PluginEvent::Type::note_on, 0, 60, 1.0};
        for (int block = 0; block < 40; ++block) {
            std::fill(left.begin(), left.end(), 0.0F);
            std::fill(right.begin(), right.end(), 0.0F);
            if (block == 0) plugin->process({left, right}, std::span{&note, 1});
            else plugin->process({left, right}, {});
            for (const float sample : left) peak = std::max(peak, std::abs(sample));
        }
        std::cout << "a held C4 peaks at " << peak << '\n';
        if (!(peak > 1e-4F)) fail("a held note must be heard");
    }
    {
        PluginWindows windows;
        const auto where = blokkily::track_instrument(0);
        QString reason;
        if (!windows.open(where, *plugin, QStringLiteral("third-party native check"), &reason)) {
            std::cerr << "FAIL: the editor did not open: " << reason.toStdString() << '\n';
            return 1;
        }
        QWindow* window = windows.window(where);
        if (window == nullptr || windows.placement(where) != PluginWindows::Placement::embedded) {
            std::cerr << "FAIL: the editor must be embedded in a window of the host\n";
            return 1;
        }
        const WId host = window->winId();
        std::vector<QSize> inside;
        QImage seen;
        int colours = 0;
        QElapsedTimer waited;
        waited.start();
        while (waited.elapsed() < 15000) {
            run_events(200);
            if (!window->isExposed()) continue;
            inside = native_probe::children(host);
            for (const auto& [means, image] : native_probe::grab(host)) {
                if (image.isNull()) continue;
                // How many colours a sample of the image holds: an editor draws
                // more than one.
                std::vector<QRgb> found;
                for (int y = 0; y < image.height(); y += std::max(1, image.height() / 40))
                    for (int x = 0; x < image.width(); x += std::max(1, image.width() / 40)) {
                        const auto pixel = image.pixel(x, y);
                        if (std::find(found.begin(), found.end(), pixel) == found.end())
                            found.push_back(pixel);
                    }
                if (static_cast<int>(found.size()) > colours) {
                    colours = static_cast<int>(found.size());
                    seen = image;
                    std::cout << "grab by " << means.toStdString() << ": " << image.width()
                              << "x" << image.height() << ", " << colours << " colours\n";
                }
            }
            if (colours > 8 && !inside.empty()) break;
        }
        const QString directory = QStringLiteral(BLOKKILY_TEST_ARTIFACTS);
        QDir{}.mkpath(directory);
        const QString file = directory + QStringLiteral("/plugin-window-%1-thirdparty-%2.png")
                                             .arg(platform, QString::fromStdString(format).toLower());
        if (!seen.isNull()) (void)seen.save(file, "PNG");
        std::cout << "host window logical " << window->width() << "x" << window->height()
                  << "; " << inside.size() << " native view(s) inside:";
        for (const auto& size : inside) std::cout << ' ' << size.width() << 'x' << size.height();
        std::cout << "; written to " << file.toStdString() << '\n';
        const bool sized = std::any_of(inside.begin(), inside.end(), [](const QSize& size) {
            return size.width() >= 100 && size.height() >= 80;
        });
        if (!sized) fail("the editor's own view, at least 100x80, must be inside the host window");
        if (window->width() < 100 || window->height() < 80)
            fail("the host window must take the editor's size");
        if (colours <= 8) fail("the editor must draw something other than a flat colour");
        if (!native_probe::request_close(host)) fail("the close request could not be made");
        QElapsedTimer closing;
        closing.start();
        while (windows.isOpen(where) && closing.elapsed() < 3000) run_events(50);
        if (windows.isOpen(where) || plugin->editor_open())
            fail("closing the window must close the editor");
    }
    plugin.reset();
    std::cout << (status == 0 ? "PASS" : "FAIL") << ": the " << format << " at " << path
              << " plays and draws its editor inside the host's " << platform.toStdString()
              << " window\n";
    return status;
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "third-party") == 0) {
        QGuiApplication app(argc, argv);
        return third_party(argv[2], argv[3]);
    }
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
