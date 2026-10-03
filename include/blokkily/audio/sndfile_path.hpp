#pragma once

#include <filesystem>
#include <string>

namespace blokkily {

// A path spelled as libsndfile's sf_open() takes it: the native bytes, except
// on Windows, where the native form is wide and sf_open() reads UTF-8.
inline std::string sndfile_path(const std::filesystem::path& path) {
#if defined(_WIN32)
    const auto utf8 = path.u8string();
    return {utf8.begin(), utf8.end()};
#else
    return path.native();
#endif
}

} // namespace blokkily
