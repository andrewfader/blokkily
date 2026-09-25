#pragma once

#include <filesystem>

namespace blokkily {

// How a project file names the files it refers to (plan §F-F). A file inside
// the project's directory is written relative to it, so a project folder can
// be moved or copied whole; anything else is referenced in place by its
// absolute path. Plain lexical operations: nothing here touches the disk.

// Returns `file` relative to `base_dir` when it lies inside it, and `file`
// unchanged otherwise (including when either is empty or `file` is already
// relative).
[[nodiscard]] std::filesystem::path to_project_relative(const std::filesystem::path& file,
                                                        const std::filesystem::path& base_dir);

// The inverse: a relative `stored` path is taken against `base_dir`; an
// absolute one, or any path when `base_dir` is empty, is returned unchanged.
[[nodiscard]] std::filesystem::path resolve_project_path(const std::filesystem::path& stored,
                                                         const std::filesystem::path& base_dir);

} // namespace blokkily
