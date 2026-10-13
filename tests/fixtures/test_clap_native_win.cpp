// The CLAP fixture's editor window on Windows (see test_clap_native.hpp): a
// child window whose class brush is #C8FF3C. Its window procedure is
// DefWindowProcW, which erases the window with that brush, so no code of the
// fixture's runs inside the host's message loop, even after it is unloaded.

#include "test_clap_native.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace blokkily::test_clap_native {
namespace {

constexpr const wchar_t* class_name = L"BlokkilyTestClapEditor";

HINSTANCE this_module() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&this_module), &module);
    return module;
}

bool registered(HINSTANCE module) {
    WNDCLASSW existing{};
    if (GetClassInfoW(module, class_name, &existing) != 0) return true;
    WNDCLASSW window_class{};
    window_class.lpfnWndProc = DefWindowProcW;
    window_class.hInstance = module;
    window_class.hbrBackground = CreateSolidBrush(RGB(0xc8, 0xff, 0x3c));
    window_class.lpszClassName = class_name;
    return RegisterClassW(&window_class) != 0;
}

} // namespace

void* attach(void* parent, std::uint32_t width, std::uint32_t height) {
    auto* host = static_cast<HWND>(parent);
    if (host == nullptr || IsWindow(host) == 0) return nullptr;
    const HINSTANCE module = this_module();
    if (!registered(module)) return nullptr;
    return CreateWindowExW(0, class_name, L"", WS_CHILD | WS_VISIBLE, 0, 0,
                           static_cast<int>(width), static_cast<int>(height), host, nullptr,
                           module, nullptr);
}

void resize(void* child, std::uint32_t width, std::uint32_t height) {
    SetWindowPos(static_cast<HWND>(child), nullptr, 0, 0, static_cast<int>(width),
                 static_cast<int>(height), SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void detach(void* child) { DestroyWindow(static_cast<HWND>(child)); }

} // namespace blokkily::test_clap_native
