#pragma once

#include <filesystem>
#include <vector>

namespace blokkily {

class SoundFontCatalog {
public:
    [[nodiscard]] static std::vector<std::filesystem::path> scan_paths(
        const std::vector<std::filesystem::path>& roots);
    [[nodiscard]] static std::vector<std::filesystem::path> system_paths();
};

} // namespace blokkily
