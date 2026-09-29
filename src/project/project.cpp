#include "blokkily/project/project.hpp"

#include "records.hpp"

#include <array>
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
        &timebase_records(),
        &chord_velocity_records(),
        &audio_records(),
        &effects_records(),
        &input_records(),
        &automation_records(),
        &metronome_records(),
        &clip_warp_records(),
        &scene_launcher_records(),
        &modulation_records(),
        &continuous_records(),
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
    if (!version || *version < oldest_readable_version || *version > format_version) {
        (void)fail(error, "unsupported project format version");
        return std::nullopt;
    }

    Project project;
    project_io::ParseContext context{project, static_cast<int>(*version), base_dir, error, {}, {}, {}, {}, {}, {}, {}, {}};

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

bool ProjectFile::save(const Project& project, const std::filesystem::path& file,
                       std::string* error) {
    std::error_code failure;
    const auto parent = file.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, failure);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return fail(error, "cannot open project file for writing");
    out << serialize(project, directory_of(file));
    if (!out) return fail(error, "failed while writing project file");
    return true;
}

std::optional<Project> ProjectFile::load(const std::filesystem::path& file, std::string* error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        (void)fail(error, "cannot open project file for reading");
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parse(buffer.str(), error, directory_of(file));
}

} // namespace blokkily
