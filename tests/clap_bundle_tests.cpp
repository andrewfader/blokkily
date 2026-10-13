// features/plugin_scanning.feature: Scenario: A CLAP plugin in a macOS bundle
// is found, loaded and played. On macOS a .clap is a bundle directory with its
// executable in Contents/MacOS, not a file. The CLAP fixture is laid out that
// way in a directory of its own (on Linux and Windows the executable inside
// is the fixture's own shared library, so the layout is proved everywhere):
// the browser's enumeration lists the bundle once, the catalog and the scan
// helper's describe it, and the production adapter instantiates it from the
// bundle's path and plays a note through it.

#include "blokkily/plugins/clap_catalog.hpp"
#include "blokkily/plugins/clap_instance.hpp"
#include "blokkily/plugins/plugin_scan.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;

namespace {
void require(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}
} // namespace

int main() {
    try {
        const auto root = std::filesystem::path(BLOKKILY_TEST_WORK) / "clap-bundles";
        std::filesystem::remove_all(root);
        const auto bundle = root / "Blokkily Bundle.clap";
        const auto executables = bundle / "Contents" / "MacOS";
        std::filesystem::create_directories(executables);
        std::filesystem::copy_file(BLOKKILY_TEST_CLAP_PATH, executables / "Blokkily Bundle");

        // Enumerated once, as the bundle, never as the file inside it.
        const auto candidates = enumerate_scan_candidates({root}, {});
        require(candidates.size() == 1 && candidates.front().format == "CLAP" &&
                    candidates.front().path.filename() == "Blokkily Bundle.clap",
                "the bundle is one CLAP candidate, found as the bundle; " +
                    std::to_string(candidates.size()) + " candidate(s)");

        // Described by the catalog and by the scan helper's code.
        const auto catalog = ClapCatalog{}.scan_paths({root});
        require(catalog.failures.empty() && catalog.plugins.size() == 1 &&
                    catalog.plugins.front().id == "dev.blokkily.test",
                "the catalog describes the bundle's plugin");
        std::string error;
        const auto records = scan_candidate(candidates.front(), &error);
        require(!records.empty() && records.front().identifier == "dev.blokkily.test",
                "the scan helper describes it too: " + error);

        // Instantiated from the bundle's path and played.
        auto plugin = ClapPluginInstance::create(bundle, "dev.blokkily.test", &error);
        require(plugin != nullptr, "the adapter instantiates the bundle: " + error);
        require(plugin->activate(48000.0, 1, 256), "the bundled plugin activates");
        std::vector<float> left(256, 0.0F), right(256, 0.0F);
        const PluginEvent note{PluginEvent::Type::note_on, 0, 60, 1.0};
        plugin->process({left, right}, std::span{&note, 1});
        require(*std::max_element(left.begin(), left.end()) > 0.1F, "and plays a note");
        plugin.reset();
        std::filesystem::remove_all(root);
    } catch (const std::exception& failure) {
        std::cerr << "clap_bundle: " << failure.what() << '\n';
        return 1;
    }
    std::cout << "clap_bundle: ok\n";
    return 0;
}
