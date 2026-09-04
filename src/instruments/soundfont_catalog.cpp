#include "blokkily/instruments/soundfont_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <system_error>

namespace blokkily {
namespace {

bool is_soundfont(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char value) { return static_cast<char>(std::tolower(value)); });
    return extension == ".sf2" || extension == ".sf3";
}

void append_home(std::vector<std::filesystem::path>& paths,
                 const char* suffix) {
    if (const auto* home = std::getenv("HOME"))
        paths.emplace_back(std::filesystem::path(home) / suffix);
}

} // namespace

std::vector<std::filesystem::path> SoundFontCatalog::scan_paths(
    const std::vector<std::filesystem::path>& roots) {
    std::vector<std::filesystem::path> found;
    for (const auto& root : roots) {
        std::error_code error;
        if (std::filesystem::is_regular_file(root, error)) {
            if (is_soundfont(root)) found.push_back(root);
            continue;
        }
        if (!std::filesystem::is_directory(root, error)) continue;
        for (std::filesystem::recursive_directory_iterator it(
                 root, std::filesystem::directory_options::skip_permission_denied, error), end;
             it != end; it.increment(error)) {
            if (error) { error.clear(); continue; }
            if (it->is_regular_file(error) && is_soundfont(it->path()))
                found.push_back(it->path());
        }
    }
    std::sort(found.begin(), found.end());
    found.erase(std::unique(found.begin(), found.end()), found.end());
    return found;
}

std::vector<std::filesystem::path> SoundFontCatalog::system_paths() {
    std::vector<std::filesystem::path> paths;
#if defined(_WIN32)
    if (const auto* app_data = std::getenv("APPDATA"))
        paths.emplace_back(std::filesystem::path(app_data) / "SoundFonts");
    if (const auto* program_data = std::getenv("PROGRAMDATA"))
        paths.emplace_back(std::filesystem::path(program_data) / "SoundFonts");
#elif defined(__APPLE__)
    append_home(paths, "Library/Audio/Sounds/Banks");
    paths.emplace_back("/Library/Audio/Sounds/Banks");
#else
    append_home(paths, ".local/share/soundfonts");
    append_home(paths, ".local/share/sounds/sf2");
    append_home(paths, ".local/share/sounds/sf3");
    paths.emplace_back("/usr/local/share/soundfonts");
    paths.emplace_back("/usr/local/share/sounds/sf2");
    paths.emplace_back("/usr/local/share/sounds/sf3");
    paths.emplace_back("/usr/share/soundfonts");
    paths.emplace_back("/usr/share/sounds/sf2");
    paths.emplace_back("/usr/share/sounds/sf3");
#endif
    return paths;
}

} // namespace blokkily
