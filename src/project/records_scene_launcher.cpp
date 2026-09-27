#include "records.hpp"

#include <algorithm>
#include <array>

namespace blokkily::project_io {

namespace {

void write_scene_launcher(const Project& project, WriteContext& context) {
    const auto& launcher = project.song.launcher;
    if (launcher.empty()) return;

    for (std::size_t s = 0; s < launcher.scenes.size(); ++s) {
        const auto& scene = launcher.scenes[s];
        context.out << "scene " << s << ' ' << (scene.name.empty() ? "\"\"" : scene.name);
        if (scene.bpm.has_value()) {
            context.out << ' ' << number(*scene.bpm);
        }
        context.out << '\n';

        for (std::size_t t = 0; t < scene.slots.size(); ++t) {
            if (scene.slots[t].has_value()) {
                const auto& slot = *scene.slots[t];
                context.out << "sceneslot " << s << ' ' << t << ' '
                            << slot.pattern << ' ' << slot.repeats << ' '
                            << static_cast<int>(slot.quantization) << ' '
                            << static_cast<int>(slot.follow_action) << '\n';
            }
        }
    }
}

bool parse_scene(const Fields& fields, ParseContext& context) {
    const auto idx = fields.integer(1);
    if (!idx || *idx < 0 || fields.tokens.size() < 3) {
        return context.fail("malformed scene record");
    }
    const std::string& name_raw = fields.tokens[2];
    std::string name = (name_raw == "\"\"") ? "" : name_raw;
    std::optional<double> bpm = std::nullopt;
    if (fields.tokens.size() >= 4) {
        const auto b = fields.real(3);
        if (b) bpm = *b;
    }

    auto& launcher = context.project.song.launcher;
    const std::size_t index = static_cast<std::size_t>(*idx);
    if (index >= launcher.scenes.size()) {
        launcher.scenes.resize(index + 1);
    }
    launcher.scenes[index].name = std::move(name);
    launcher.scenes[index].bpm = bpm;
    return true;
}

bool parse_sceneslot(const Fields& fields, ParseContext& context) {
    const auto s = fields.integer(1);
    const auto t = fields.integer(2);
    const auto pat = fields.integer(3);
    const auto rep = fields.integer(4);
    const auto quant = fields.integer(5);
    const auto follow = fields.integer(6);

    if (!s || !t || !pat || !rep || !quant || !follow ||
        *s < 0 || *t < 0 || *pat < 0 || *rep < 0) {
        return context.fail("malformed sceneslot record");
    }

    auto& launcher = context.project.song.launcher;
    const std::size_t s_idx = static_cast<std::size_t>(*s);
    const std::size_t t_idx = static_cast<std::size_t>(*t);

    if (s_idx >= launcher.scenes.size()) {
        launcher.scenes.resize(s_idx + 1);
    }
    SceneSlot slot{
        .pattern = static_cast<std::size_t>(*pat),
        .repeats = static_cast<std::uint32_t>(*rep),
        .quantization = static_cast<LaunchQuantization>(*quant),
        .follow_action = static_cast<FollowAction>(*follow)
    };
    launcher.set_slot(s_idx, t_idx, slot);
    return true;
}

constexpr std::array scene_launcher_handlers{
    RecordHandler{"scene", parse_scene},
    RecordHandler{"sceneslot", parse_sceneslot}
};

} // namespace

const RecordModule& scene_launcher_records() {
    static const RecordModule module{"scene_launcher", write_scene_launcher, scene_launcher_handlers, nullptr};
    return module;
}

} // namespace blokkily::project_io
