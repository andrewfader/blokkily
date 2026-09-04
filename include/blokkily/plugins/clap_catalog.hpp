#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace blokkily {

struct ClapDescriptor {
    std::filesystem::path library;
    std::string id;
    std::string name;
    std::string vendor;
    std::string version;
    std::vector<std::string> features;
};

struct ClapScanFailure {
    std::filesystem::path library;
    std::string reason;
};

struct ClapScanResult {
    std::vector<ClapDescriptor> plugins;
    std::vector<ClapScanFailure> failures;
};

class ClapCatalog {
public:
    [[nodiscard]] ClapScanResult scan_file(const std::filesystem::path& file) const;
    [[nodiscard]] ClapScanResult scan_paths(
        const std::vector<std::filesystem::path>& roots) const;
    [[nodiscard]] static std::vector<std::filesystem::path> system_paths();
};

} // namespace blokkily

