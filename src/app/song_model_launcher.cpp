// The scene launcher in the song model (phase 2, wave 6.1). The grid is part
// of the song: every edit is a step of history and is saved with the project,
// and reaches the running engine as a recompile (a structure change), never a
// rebuild. A take the launcher recorded is printed into the arrangement here,
// one step of history per take.

#include "song_model.hpp"

#include <QtGlobal>

#include <algorithm>
#include <iterator>
#include <optional>

namespace {

constexpr const char* quantization_names[] = {"NONE", "1/16", "BEAT", "BAR", "2 BARS", "4 BARS"};
constexpr const char* follow_names[] = {"LOOP", "STOP", "NEXT", "PREV", "FIRST", "LAST",
                                        "RANDOM", "AGAIN"};

std::optional<blokkily::LaunchQuantization> quantization_named(const QString& name) {
    for (std::size_t index = 0; index < std::size(quantization_names); ++index)
        if (name == QLatin1String(quantization_names[index]))
            return static_cast<blokkily::LaunchQuantization>(index);
    return std::nullopt;
}

std::optional<blokkily::FollowAction> follow_named(const QString& name) {
    for (std::size_t index = 0; index < std::size(follow_names); ++index)
        if (name == QLatin1String(follow_names[index]))
            return static_cast<blokkily::FollowAction>(index);
    return std::nullopt;
}

QString quantization_name(blokkily::LaunchQuantization quantization) {
    const auto index = static_cast<std::size_t>(quantization);
    return index < std::size(quantization_names) ? QLatin1String(quantization_names[index])
                                                 : QStringLiteral("BAR");
}

QString follow_name(blokkily::FollowAction action) {
    const auto index = static_cast<std::size_t>(action);
    return index < std::size(follow_names) ? QLatin1String(follow_names[index])
                                           : QStringLiteral("LOOP");
}

} // namespace

bool SongModel::validCell(int scene, int track) const {
    return scene >= 0 && static_cast<std::size_t>(scene) < song_.launcher.scenes.size() &&
           validTrack(track);
}

QVariantList SongModel::launcherScenes() const {
    QVariantList rows;
    const auto& launcher = song_.launcher;
    for (std::size_t scene = 0; scene < launcher.scenes.size(); ++scene) {
        QVariantList cells;
        for (std::size_t track = 0; track < song_.tracks.size(); ++track) {
            const auto& slot = launcher.slot(scene, track);
            const bool filled = slot.has_value() && slot->pattern < song_.patterns.size();
            cells.push_back(QVariantMap{
                {"track", static_cast<int>(track)},
                {"filled", filled},
                {"pattern", filled ? static_cast<int>(slot->pattern) : -1},
                {"name", filled ? QString::fromStdString(song_.patterns[slot->pattern].name)
                                : QString()},
                {"repeats", filled ? static_cast<int>(slot->repeats) : 0},
                {"quantization", quantization_name(filled ? slot->quantization
                                                          : blokkily::LaunchQuantization::bar)},
                {"follow", follow_name(filled ? slot->follow_action : blokkily::FollowAction::none)},
            });
        }
        rows.push_back(QVariantMap{{"index", static_cast<int>(scene)},
                                   {"name", QString::fromStdString(launcher.scenes[scene].name)},
                                   {"cells", cells}});
    }
    return rows;
}

QString SongModel::launcherQuantization() const {
    return quantization_name(song_.launcher.quantization);
}

QStringList SongModel::launchQuantizations() const {
    QStringList names;
    for (const auto* name : quantization_names) names.push_back(QLatin1String(name));
    return names;
}

QStringList SongModel::followActions() const {
    QStringList names;
    for (const auto* name : follow_names) names.push_back(QLatin1String(name));
    return names;
}

int SongModel::addScene() {
    checkpoint();
    // Numbered past the highest in use, as tracks are.
    int number = static_cast<int>(song_.launcher.scenes.size()) + 1;
    const auto taken = [this](int candidate) {
        const auto name = QString("SCENE %1").arg(candidate).toStdString();
        return std::any_of(song_.launcher.scenes.begin(), song_.launcher.scenes.end(),
                           [&](const blokkily::Scene& scene) { return scene.name == name; });
    };
    while (taken(number)) ++number;
    const auto index = song_.launcher.add_scene(QString("SCENE %1").arg(number).toStdString());
    notifyStructureChanged();
    return static_cast<int>(index);
}

bool SongModel::removeScene(int scene) {
    if (scene < 0 || static_cast<std::size_t>(scene) >= song_.launcher.scenes.size()) return false;
    checkpoint();
    (void)song_.launcher.remove_scene(static_cast<std::size_t>(scene));
    notifyStructureChanged();
    return true;
}

bool SongModel::renameScene(int scene, const QString& name) {
    if (scene < 0 || static_cast<std::size_t>(scene) >= song_.launcher.scenes.size()) return false;
    const auto shown = name.trimmed().left(24).toUpper().toStdString();
    if (shown.empty()) return false;
    auto& target = song_.launcher.scenes[static_cast<std::size_t>(scene)].name;
    if (target == shown) return true;
    checkpoint();
    target = shown;
    emit songChanged();
    return true;
}

bool SongModel::setLauncherCell(int scene, int track, int pattern) {
    if (!validCell(scene, track) || pattern < 0 ||
        static_cast<std::size_t>(pattern) >= song_.patterns.size())
        return false;
    const auto s = static_cast<std::size_t>(scene);
    const auto t = static_cast<std::size_t>(track);
    auto slot = song_.launcher.slot(s, t).value_or(blokkily::SceneSlot{});
    if (song_.launcher.slot(s, t).has_value() && slot.pattern == static_cast<std::size_t>(pattern))
        return true;
    checkpoint();
    slot.pattern = static_cast<std::size_t>(pattern);
    song_.launcher.set_slot(s, t, slot);
    notifyStructureChanged();
    return true;
}

bool SongModel::clearLauncherCell(int scene, int track) {
    if (!validCell(scene, track)) return false;
    const auto s = static_cast<std::size_t>(scene);
    const auto t = static_cast<std::size_t>(track);
    if (!song_.launcher.slot(s, t).has_value()) return false;
    checkpoint();
    song_.launcher.clear_slot(s, t);
    notifyStructureChanged();
    return true;
}

bool SongModel::setCellRepeats(int scene, int track, int repeats) {
    if (!validCell(scene, track)) return false;
    const auto s = static_cast<std::size_t>(scene);
    const auto t = static_cast<std::size_t>(track);
    auto slot = song_.launcher.slot(s, t);
    if (!slot) return false;
    const auto next = static_cast<std::uint32_t>(qBound(0, repeats, 64));
    if (slot->repeats == next) return true;
    checkpoint();
    slot->repeats = next;
    song_.launcher.set_slot(s, t, *slot);
    notifyStructureChanged();
    return true;
}

bool SongModel::setCellFollow(int scene, int track, const QString& follow) {
    const auto action = follow_named(follow);
    if (!action || !validCell(scene, track)) return false;
    const auto s = static_cast<std::size_t>(scene);
    const auto t = static_cast<std::size_t>(track);
    auto slot = song_.launcher.slot(s, t);
    if (!slot) return false;
    if (slot->follow_action == *action) return true;
    checkpoint();
    slot->follow_action = *action;
    // A follow action needs a count of loops to wait for; one is the least.
    if (*action != blokkily::FollowAction::none && slot->repeats == 0) slot->repeats = 1;
    song_.launcher.set_slot(s, t, *slot);
    notifyStructureChanged();
    return true;
}

bool SongModel::setCellQuantization(int scene, int track, const QString& quantization) {
    const auto value = quantization_named(quantization);
    if (!value || !validCell(scene, track)) return false;
    const auto s = static_cast<std::size_t>(scene);
    const auto t = static_cast<std::size_t>(track);
    auto slot = song_.launcher.slot(s, t);
    if (!slot) return false;
    if (slot->quantization == *value) return true;
    checkpoint();
    slot->quantization = *value;
    song_.launcher.set_slot(s, t, *slot);
    notifyStructureChanged();
    return true;
}

bool SongModel::setLauncherQuantization(const QString& quantization) {
    const auto value = quantization_named(quantization);
    if (!value) return false;
    if (song_.launcher.quantization == *value) return true;
    checkpoint();
    song_.launcher.quantization = *value;
    notifyStructureChanged();
    return true;
}

bool SongModel::printLauncherTake(const blokkily::LauncherTake& take) {
    auto printed = song_;
    if (!printed.print_take(take.track, take.pattern, take.start, take.end, take.cycle) ||
        !printed.consistent())
        return false;
    checkpoint();
    song_.clips = std::move(printed.clips);
    notifyStructureChanged();
    return true;
}
