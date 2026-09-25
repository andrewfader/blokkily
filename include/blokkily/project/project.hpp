#pragma once

#include "blokkily/model/song.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace blokkily {

// A session: the arrangement and its name. Everything musical, the tempo map
// included, lives in the song, so saving a project and saving a song are the
// same act.
struct Project {
    std::string name = "Untitled";
    Song song;

    [[nodiscard]] Pattern& pattern(std::size_t index = 0) { return song.pattern(index); }
    [[nodiscard]] const Pattern& pattern(std::size_t index = 0) const {
        return song.pattern(index);
    }
};

// A versioned, line-oriented, deterministic project file. Saving the same
// project twice produces byte-identical output, so projects diff cleanly and
// round-trip exactly.
class ProjectFile {
public:
    // 2 added the parameter index and automation/modulation kind to locks.
    // 3 replaced the single pattern and flat instrument list with a song:
    // named patterns, mixer tracks that own their instrument, and clips.
    // 4 added the song's tuning and scale, and the retune each note and each
    // chord voice carries away from its twelve-tone key.
    // 5 changed no record: from here on every feature adds records of its own
    // (src/project/records_<feature>.cpp), and a file without them loads with
    // that feature's defaults. Format 4 files therefore still load unchanged.
    static constexpr int format_version = 5;
    static constexpr int oldest_readable_version = 4;

    [[nodiscard]] static bool save(const Project& project,
                                   const std::filesystem::path& file,
                                   std::string* error = nullptr);
    [[nodiscard]] static std::optional<Project> load(const std::filesystem::path& file,
                                                     std::string* error = nullptr);

    // Exposed so that the round-trip can be tested without touching a disk.
    // `base_dir` is the directory the project file lives in (save and load pass
    // the file's own directory); records that name files write them relative
    // to it when they lie inside it, and resolve them against it when read.
    // See blokkily/project/paths.hpp.
    [[nodiscard]] static std::string serialize(const Project& project,
                                               const std::filesystem::path& base_dir = {});
    [[nodiscard]] static std::optional<Project> parse(const std::string& text,
                                                      std::string* error = nullptr,
                                                      const std::filesystem::path& base_dir = {});
};

} // namespace blokkily
