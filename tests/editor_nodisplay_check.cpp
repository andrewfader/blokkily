// A machine with no display at all: a VST3 editor is asked for with DISPLAY
// unset, and then set to the empty string, and each request is refused with
// "Plugin window needs an X11 display" before JUCE is asked for anything.
// JUCE would turn an empty DISPLAY into ":0.0" and try another user's server
// (tens of seconds on this machine), and an editor peer without X segfaults;
// neither may happen. The instrument keeps playing afterwards.
//
// features/plugin_windows.feature:
//   Scenario: With no display at all, asking for a VST3 editor is refused at once
//
// The fixture is instantiated first, under the nonexistent DISPLAY CTest sets,
// because the plugin's own copy of JUCE opens X when it is instantiated; only
// the host's side is under test here. Run as its own process because it
// changes the environment.

#include "blokkily/plugins/vst3_instance.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <span>
#include <string>
#include <vector>

namespace {
struct Host final : blokkily::EditorHost {
    int calls = 0;
    void request_resize(std::uint32_t, std::uint32_t) override { ++calls; }
    void request_show() override { ++calls; }
    void request_hide() override { ++calls; }
    void closed() override { ++calls; }
};

// DISPLAY set to `value`, or unset when it is null.
void set_display(const char* value) {
#if defined(_WIN32)
    _putenv_s("DISPLAY", value == nullptr ? "" : value);
#else
    if (value == nullptr)
        unsetenv("DISPLAY");
    else
        setenv("DISPLAY", value, 1);
#endif
}

bool refused(blokkily::Vst3PluginInstance& plugin, const char* situation) {
    using namespace blokkily;
    Host host;
    const NativeParent parent{WindowApi::x11, 0x1, 1.0};
    bool ok = true;
    for (const NativeParent* where : {&parent, static_cast<const NativeParent*>(nullptr)}) {
        std::string error;
        const bool opened = plugin.open_editor(where, host, nullptr, &error);
        const bool right = !opened && error == "Plugin window needs an X11 display" &&
                           !plugin.editor_open() && host.calls == 0;
        std::cout << situation << (where ? ", embedded" : ", floating") << ": "
                  << (opened ? "OPENED" : "refused") << " \"" << error << "\"\n";
        ok = ok && right;
    }
    return ok;
}
} // namespace

int main() {
    using namespace blokkily;
    std::string error;
    auto plugin = Vst3PluginInstance::create(BLOKKILY_TEST_VST3_PATH, 0, &error);
    if (!plugin) {
        std::cerr << "the VST3 fixture must instantiate: " << error << '\n';
        return 1;
    }
    if (!plugin->has_editor()) {
        std::cerr << "the VST3 fixture must have an editor\n";
        return 1;
    }
    bool ok = true;
    set_display(nullptr);
    ok = refused(*plugin, "DISPLAY unset") && ok;
    set_display("");
    ok = refused(*plugin, "DISPLAY empty") && ok;

    // Refusing left the instrument as it was.
    std::vector<float> left(256), right(256);
    const PluginEvent note{PluginEvent::Type::note_on, 0, 60, 1.0};
    if (!plugin->activate(48000.0, 1, 256)) ok = false;
    plugin->process({left, right}, std::span{&note, 1});
    if (std::abs(left[128] - 0.25F) > 1e-4F) {
        std::cerr << "the instrument no longer plays after the refusals\n";
        ok = false;
    }
    plugin.reset();
    std::cout << (ok ? "PASS" : "FAIL") << ": no display, no editor, no crash\n";
    return ok ? 0 : 1;
}
