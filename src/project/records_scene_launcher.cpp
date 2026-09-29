// The scene launcher's records (phase 2, wave 6.1):
//
//   launcher <quantization>
//   scene <index> <name>
//   sceneslot <scene> <track> <pattern> <repeats> <quantization> <follow>
//
// The name is escaped like every other name in the file (record_io.hpp), so a
// scene called "Verse 2" is one token. Files written before names were
// escaped wrote an empty name as "" and could carry a scene tempo after it;
// both still load, the tempo ignored (a scene no longer has one). Every value
// is checked against its range: a file naming a quantization or a follow
// action that does not exist is refused, never cast.

#include "records.hpp"

#include <algorithm>
#include <array>

namespace blokkily::project_io {

namespace {

// Bounds a malformed file cannot push the grid past.
constexpr long long maximum_scenes = 1024;
constexpr long long maximum_columns = 1024;

void write_scene_launcher(const Project& project, WriteContext& context) {
    const auto& launcher = project.song.launcher;
    if (launcher.empty() && launcher.quantization == LaunchQuantization::bar) return;

    context.out << "launcher " << static_cast<int>(launcher.quantization) << '\n';
    for (std::size_t s = 0; s < launcher.scenes.size(); ++s) {
        const auto& scene = launcher.scenes[s];
        context.out << "scene " << s << ' ' << escape(scene.name) << '\n';
        for (std::size_t t = 0; t < scene.cells.size(); ++t) {
            if (!scene.cells[t].has_value()) continue;
            const auto& slot = *scene.cells[t];
            context.out << "sceneslot " << s << ' ' << t << ' ' << slot.pattern << ' '
                        << slot.repeats << ' ' << static_cast<int>(slot.quantization) << ' '
                        << static_cast<int>(slot.follow_action) << '\n';
        }
    }
}

bool parse_launcher(const Fields& fields, ParseContext& context) {
    const auto quantization = fields.integer(1);
    if (!fields.count(2) || !quantization || *quantization < 0 ||
        *quantization >= launch_quantization_count)
        return context.fail("malformed launcher record");
    context.project.song.launcher.quantization = static_cast<LaunchQuantization>(*quantization);
    return true;
}

bool parse_scene(const Fields& fields, ParseContext& context) {
    const auto index = fields.integer(1);
    // An older file may carry a tempo after the name: read, then ignored.
    if (!(fields.count(3) || fields.count(4)) || !index || *index < 0 ||
        *index >= maximum_scenes)
        return context.fail("malformed scene record");
    if (fields.count(4) && !fields.real(3)) return context.fail("malformed scene record");
    std::optional<std::string> name =
        fields.tokens[2] == "\"\"" ? std::optional<std::string>{std::string{}} : fields.text(2);
    if (!name) return context.fail("malformed scene record");

    auto& launcher = context.project.song.launcher;
    const auto at = static_cast<std::size_t>(*index);
    if (at >= launcher.scenes.size()) launcher.scenes.resize(at + 1);
    launcher.scenes[at].name = std::move(*name);
    return true;
}

bool parse_sceneslot(const Fields& fields, ParseContext& context) {
    const auto scene = fields.integer(1);
    const auto track = fields.integer(2);
    const auto pattern = fields.integer(3);
    const auto repeats = fields.integer(4);
    const auto quantization = fields.integer(5);
    const auto follow = fields.integer(6);
    if (!fields.count(7) || !scene || !track || !pattern || !repeats || !quantization ||
        !follow || *scene < 0 || *scene >= maximum_scenes || *track < 0 ||
        *track >= maximum_columns || *pattern < 0 || *repeats < 0 || *repeats > 0xFFFF ||
        *quantization < 0 || *quantization >= launch_quantization_count || *follow < 0 ||
        *follow >= follow_action_count)
        return context.fail("malformed sceneslot record");

    auto& launcher = context.project.song.launcher;
    const auto at = static_cast<std::size_t>(*scene);
    if (at >= launcher.scenes.size()) launcher.scenes.resize(at + 1);
    launcher.set_slot(at, static_cast<std::size_t>(*track),
                      SceneSlot{.pattern = static_cast<std::size_t>(*pattern),
                                .repeats = static_cast<std::uint32_t>(*repeats),
                                .quantization = static_cast<LaunchQuantization>(*quantization),
                                .follow_action = static_cast<FollowAction>(*follow)});
    return true;
}

constexpr std::array scene_launcher_handlers{
    RecordHandler{"launcher", parse_launcher},
    RecordHandler{"scene", parse_scene},
    RecordHandler{"sceneslot", parse_sceneslot},
};

} // namespace

const RecordModule& scene_launcher_records() {
    static const RecordModule module{"scene_launcher", write_scene_launcher,
                                     scene_launcher_handlers, nullptr};
    return module;
}

} // namespace blokkily::project_io
