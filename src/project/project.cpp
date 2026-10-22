#include "blokkily/project/project.hpp"

#include "records.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

namespace blokkily {

namespace project_io {

// The registered record modules, in write and finish order. A feature that
// stores something in the project adds its records_<feature>.cpp and one line
// here; it never edits another module's records.
std::span<const RecordModule* const> record_modules() {
    static const std::array modules{
        &core_records(),
        &lyric_records(),
        &timebase_records(),
        &chord_velocity_records(),
        &lock_target_records(),
        &audio_records(),
        &effects_records(),
        &input_records(),
        &automation_records(),
        &metronome_records(),
        &loop_records(),
        &swing_records(),
        &colour_records(),
        &comping_records(),
        &freeze_records(),
        &arpeggiator_records(),
        &clip_warp_records(),
        &scene_launcher_records(),
        &modulation_records(),
        &midi_learn_records(),
        &continuous_records(),
        &expression_records(),
        &sections_records(),
        &notation_records(),
        &version_records(),
    };
    return modules;
}

} // namespace project_io

namespace {

using project_io::fail;

// The directory a project file's relative paths are taken against.
std::filesystem::path directory_of(const std::filesystem::path& file) {
    std::error_code failure;
    const auto absolute = std::filesystem::absolute(file, failure);
    return (failure ? file : absolute).parent_path();
}

const project_io::RecordHandler* find_handler(std::string_view record) {
    for (const auto* module : project_io::record_modules())
        for (const auto& handler : module->handlers)
            if (handler.record == record) return &handler;
    return nullptr;
}

} // namespace

std::string ProjectFile::serialize(const Project& project, const std::filesystem::path& base_dir) {
    std::ostringstream out;
    out << "blokkily-project " << format_version << '\n';
    project_io::WriteContext context{out, base_dir};
    for (const auto* module : project_io::record_modules()) module->write(project, context);
    return out.str();
}

std::optional<Project> ProjectFile::parse(const std::string& text, std::string* error,
                                          const std::filesystem::path& base_dir) {
    std::istringstream stream(text);
    std::string line;
    if (!std::getline(stream, line)) {
        (void)fail(error, "project file is empty");
        return std::nullopt;
    }
    const auto header = project_io::split(line);
    if (!header.count(2) || header.tokens[0] != "blokkily-project") {
        (void)fail(error, "not a Blokkily project file");
        return std::nullopt;
    }
    const auto version = header.integer(1);
    if (!version) {
        (void)fail(error, "the project file's format version is unreadable");
        return std::nullopt;
    }
    if (*version > format_version) {
        (void)fail(error, "made by a newer Blokkily (format " + std::to_string(*version) +
                              "; this one reads formats " + std::to_string(oldest_readable_version) +
                              " to " + std::to_string(format_version) + ")");
        return std::nullopt;
    }
    if (*version < oldest_readable_version) {
        (void)fail(error, "made by a Blokkily too old to read (format " + std::to_string(*version) +
                              "; this one reads formats " + std::to_string(oldest_readable_version) +
                              " to " + std::to_string(format_version) + ")");
        return std::nullopt;
    }

    Project project;
    project_io::ParseContext context{project, static_cast<int>(*version), base_dir, error,
                                     {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {},
                                     {}, {}, false, {}};

    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const auto fields = project_io::split(line);
        if (fields.tokens.empty()) continue;
        const std::string& record = fields.tokens.front();
        const auto* handler = find_handler(record);
        if (handler == nullptr) {
            (void)fail(error, "unknown record: " + record);
            return std::nullopt;
        }
        if (!handler->parse(fields, context)) return std::nullopt;
    }

    for (const auto* module : project_io::record_modules())
        if (module->finish != nullptr && !module->finish(context)) return std::nullopt;

    std::string why;
    if (!project.song.consistent(&why)) {
        (void)fail(error, why);
        return std::nullopt;
    }
    return project;
}

namespace {

// Why the last file operation failed, as the system says it.
std::string system_reason() {
    return errno != 0 ? std::string(std::strerror(errno)) : std::string("unknown error");
}

enum class Written { whole, unopened, broken };

// Writes `text` to `file` from the start, truncating it; `why` says what
// went wrong.
Written write_whole(const std::filesystem::path& file, const std::string& text, std::string* why) {
    errno = 0;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) {
        *why = "cannot open " + file.string() + " for writing: " + system_reason();
        return Written::unopened;
    }
    out << text;
    out.flush();
    if (out) out.close();
    if (!out) {
        *why = "could not write " + file.string() + ": " + system_reason();
        return Written::broken;
    }
    return Written::whole;
}

} // namespace

bool ProjectFile::save(const Project& project, const std::filesystem::path& file,
                       std::string* error) {
    std::error_code failure;
    const auto parent = file.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, failure);
    const auto text = serialize(project, directory_of(file));
    // Written beside the file and moved over it once whole, so a save that
    // fails half way (a full disk) leaves the song that was there.
    auto temporary = file;
    temporary += ".saving";
    std::string why;
    switch (write_whole(temporary, text, &why)) {
    case Written::whole:
        std::filesystem::rename(temporary, file, failure);
        if (!failure) return true;
        std::filesystem::remove(temporary, failure);
        return fail(error, "could not replace " + file.string() + ": " + failure.message());
    case Written::broken:
        std::filesystem::remove(temporary, failure);
        return fail(error, why);
    case Written::unopened:
        break;
    }
    // A folder that takes no new file, where only the song itself is
    // writable, has it written in place as before.
    if (!std::filesystem::exists(file, failure))
        return fail(error, "cannot write " + file.string() + why.substr(why.rfind(':')));
    return write_whole(file, text, &why) == Written::whole || fail(error, why);
}

std::optional<Project> ProjectFile::load(const std::filesystem::path& file, std::string* error) {
    errno = 0;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        (void)fail(error, "cannot open " + file.string() + ": " + system_reason());
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parse(buffer.str(), error, directory_of(file));
}

} // namespace blokkily
