#pragma once

// The project file is a table of record modules (plan §F-F). The core module
// owns every record that format 4 defined; each later feature adds its own
// records in its own src/project/records_<feature>.cpp and appends one entry to
// record_modules() in project.cpp. Records are additive: a file without a
// feature's records loads with that feature's defaults, so format 5 never needs
// a second version bump or a version check inside a handler.
//
// Internal to src/project.

#include "blokkily/project/project.hpp"
#include "record_io.hpp"

#include <filesystem>
#include <ostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace blokkily::project_io {

// Patterns are rebuilt only after the whole file is read, so a trigger record
// can be checked against the pattern it names no matter where it appears.
struct PatternDraft {
    std::string name;
    Tick length = 1920;
    Tick ticks_per_beat = 480;
    std::vector<Trigger> triggers;

    [[nodiscard]] Trigger* find(EventId id);
};

struct WriteContext {
    std::ostream& out;
    // The directory the project file lives in, or empty when there is none.
    // Records that name files write them through to_project_relative.
    const std::filesystem::path& base_dir;
};

// Everything a record handler may read or build while a file is parsed. The
// core module's finish step moves the drafts, tracks and clips into
// project.song; modules that run after it see the finished song.
struct ParseContext {
    Project& project;
    int version = 0;
    const std::filesystem::path& base_dir;
    std::string* error = nullptr;

    std::vector<PatternDraft> drafts;
    std::vector<Track> tracks;
    std::vector<Clip> clips;

    // Records the reason and returns false.
    bool fail(std::string message) const { return project_io::fail(error, std::move(message)); }
};

struct RecordHandler {
    std::string_view record;
    bool (*parse)(const Fields& fields, ParseContext& context);
};

struct RecordModule {
    std::string_view name;
    // Appends this module's records. Called in table order, so the output of a
    // given project is always the same bytes.
    void (*write)(const Project& project, WriteContext& context);
    std::span<const RecordHandler> handlers;
    // Runs once after every line has been read, in table order. May be null.
    bool (*finish)(ParseContext& context);
};

// Records defined by format 4: name, tempo, master, tuning, scale, harmony,
// pattern, trigger, lock, track, clip.
[[nodiscard]] const RecordModule& core_records();

// Chord velocity and voice length: voicevel, voicelen (records_chord_velocity.cpp).
[[nodiscard]] const RecordModule& chord_velocity_records();

// Program schema (item 1.5): audiofile, audioclip; return, insert, send;
// input, record-offset; automode, automation. Each record naming a track or a
// return comes after that bus's own record.
[[nodiscard]] const RecordModule& audio_records();
[[nodiscard]] const RecordModule& effects_records();
[[nodiscard]] const RecordModule& input_records();
[[nodiscard]] const RecordModule& automation_records();

// The registered modules, in the order they are written and finished.
[[nodiscard]] std::span<const RecordModule* const> record_modules();

} // namespace blokkily::project_io
