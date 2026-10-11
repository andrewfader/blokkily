#include "blokkily/project/paths.hpp"

namespace blokkily {

namespace {

// "/a/b/" and "/a/b" name the same directory, but lexically_relative does not
// treat them alike, so a trailing separator is dropped first.
std::filesystem::path directory(const std::filesystem::path& base_dir) {
    auto normal = base_dir.lexically_normal();
    if (!normal.has_filename() && normal.has_relative_path()) normal = normal.parent_path();
    return normal;
}

// Whether a path is anchored at a root. On Windows "/music/x.wav" has a root
// directory but no drive, so is_absolute() calls it relative; a project file
// written on another system names files that way, and it is no less rooted.
bool rooted(const std::filesystem::path& path) { return path.has_root_directory(); }

} // namespace

std::filesystem::path to_project_relative(const std::filesystem::path& file,
                                          const std::filesystem::path& base_dir) {
    if (file.empty() || base_dir.empty() || !rooted(file)) return file;
    const auto relative = file.lexically_normal().lexically_relative(directory(base_dir));
    if (relative.empty() || relative == "." || *relative.begin() == "..") return file;
    return relative;
}

std::filesystem::path resolve_project_path(const std::filesystem::path& stored,
                                           const std::filesystem::path& base_dir) {
    if (stored.empty() || base_dir.empty() || rooted(stored)) return stored;
    return (directory(base_dir) / stored).lexically_normal();
}

} // namespace blokkily
