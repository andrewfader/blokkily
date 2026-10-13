// native_window_probe.hpp on Windows: the host's HWND, its child windows, and
// its contents copied from the screen and rendered by PrintWindow (which, with
// PW_RENDERFULLCONTENT, includes child windows as the compositor draws them).

#include "native_window_probe.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cmath>

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace native_probe {

namespace {

double scale_of(HWND window) {
    const UINT dpi = GetDpiForWindow(window);
    return dpi > 0 ? dpi / 96.0 : 1.0;
}

// The client area of `window`, `width` x `height` physical pixels, drawn by
// `draw` into a 32-bit DIB.
template <typename Draw>
QImage capture(int width, int height, Draw draw) {
    if (width <= 0 || height <= 0) return {};
    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;   // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    QImage result;
    if (bitmap != nullptr && bits != nullptr) {
        HGDIOBJ previous = SelectObject(memory, bitmap);
        if (draw(memory, screen)) {
            result = QImage(static_cast<const uchar*>(bits), width, height, width * 4,
                            QImage::Format_RGB32).copy();
        }
        SelectObject(memory, previous);
    }
    if (bitmap != nullptr) DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    return result;
}

} // namespace

std::vector<QSize> children(WId host) {
    std::vector<QSize> found;
    auto* window = reinterpret_cast<HWND>(host);
    if (window == nullptr || IsWindow(window) == 0) return found;
    for (HWND child = GetWindow(window, GW_CHILD); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        RECT rect{};
        if (GetWindowRect(child, &rect) == 0) continue;
        const double scale = scale_of(child);
        found.emplace_back(static_cast<int>(std::lround((rect.right - rect.left) / scale)),
                           static_cast<int>(std::lround((rect.bottom - rect.top) / scale)));
    }
    return found;
}

std::vector<std::pair<QString, QImage>> grab(WId host) {
    std::vector<std::pair<QString, QImage>> grabs;
    auto* window = reinterpret_cast<HWND>(host);
    RECT client{};
    if (window == nullptr || GetClientRect(window, &client) == 0) {
        grabs.emplace_back(QStringLiteral("screen"), QImage());
        grabs.emplace_back(QStringLiteral("PrintWindow"), QImage());
        return grabs;
    }
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    POINT origin{0, 0};
    ClientToScreen(window, &origin);
    grabs.emplace_back(QStringLiteral("screen"), capture(width, height, [&](HDC memory, HDC screen) {
        return BitBlt(memory, 0, 0, width, height, screen, origin.x, origin.y, SRCCOPY) != 0;
    }));
    grabs.emplace_back(QStringLiteral("PrintWindow"),
                       capture(width, height, [&](HDC memory, HDC) {
                           return PrintWindow(window, memory,
                                              PW_CLIENTONLY | PW_RENDERFULLCONTENT) != 0;
                       }));
    return grabs;
}

bool request_close(WId host) {
    auto* window = reinterpret_cast<HWND>(host);
    if (window == nullptr) return false;
    return PostMessageW(GetAncestor(window, GA_ROOT), WM_CLOSE, 0, 0) != 0;
}

} // namespace native_probe
