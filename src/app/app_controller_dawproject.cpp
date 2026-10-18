// DAWproject export (features/dawproject.feature): the song as the open
// exchange format other hosts read, with each running plugin's state as that
// host would load it.

#include "app_controller.hpp"
#include "song_model.hpp"

#include "blokkily/project/dawproject.hpp"
#include "blokkily/project/project.hpp"
#include "blokkily/plugins/vst3_instance.hpp"
#include "engine_graph.hpp"

#include <QFileInfo>
#include <QUrl>

namespace {

QString local_file(const QString& value) {
    const QUrl url(value);
    return url.isLocalFile() ? url.toLocalFile() : value;
}

} // namespace

bool AppController::exportDawProjectFile(const QString& path) {
    if (song_ == nullptr) return false;
    const auto file = local_file(path);
    blokkily::DawProjectOptions options;
    options.application_version = BLOKKILY_VERSION;
    options.title = QFileInfo(file).completeBaseName().toStdString();
    // The states come from the instances the song is playing through, so
    // what another host loads is what is heard here.
    if (engine_)
        for (const auto where : engine_->processor_addresses())
            if (auto* instance = engine_->processor(where))
                if (auto state = instance->portable_state())
                    options.states.emplace_back(where, std::move(*state));
    std::string error;
    const auto report = blokkily::write_dawproject(file.toStdString(), song_->song(), options, &error);
    dawproject_omitted_.clear();
    if (!report) {
        export_status_ = QString("DAWproject export failed · %1").arg(QString::fromStdString(error));
        emit exportStatusChanged();
        return false;
    }
    for (const auto& line : report->omitted) dawproject_omitted_.append(QString::fromStdString(line));
    export_status_ = QString("%1 · %2 track%3 · %4 note%5 · %6 plugin state%7")
                         .arg(QFileInfo(file).fileName())
                         .arg(report->tracks)
                         .arg(report->tracks == 1 ? "" : "s")
                         .arg(report->notes)
                         .arg(report->notes == 1 ? "" : "s")
                         .arg(report->plugin_states)
                         .arg(report->plugin_states == 1 ? "" : "s");
    if (!report->omitted.empty())
        export_status_ += QString(" · %1 left out: %2")
                              .arg(report->omitted.size())
                              .arg(dawproject_omitted_.join("; "));
    emit exportStatusChanged();
    return true;
}

bool AppController::importDawProjectFile(const QString& path) {
    if (song_ == nullptr) return false;
    const auto file = local_file(path);
    // A device is the installed plugin of its id (CLAP), or of its name
    // (VST3: the catalog does not hold class ids).
    const auto installed = plugins_;
    const blokkily::DawProjectLocator locate =
        [&installed](const blokkily::DawProjectDevice& device) -> std::optional<blokkily::PluginSlot> {
        const bool clap = device.element == "ClapPlugin";
        for (const auto& value : installed) {
            const auto entry = value.toMap();
            const auto format = entry.value("format").toString();
            const auto identifier = entry.value("identifier").toString().toStdString();
            const auto name = entry.value("name").toString();
            const bool match =
                clap ? format == "CLAP" && identifier == device.device_id
                     : format == "VST3" && (identifier == device.device_id ||
                                            name.compare(QString::fromStdString(device.device_name),
                                                         Qt::CaseInsensitive) == 0);
            if (match)
                return blokkily::PluginSlot{format.toStdString(), entry.value("path").toString().toStdString(),
                                            identifier, {}};
        }
        return std::nullopt;
    };
    std::string error;
    auto imported = blokkily::import_dawproject(file.toStdString(), recordingDirectory(), locate, &error);
    dawproject_omitted_.clear();
    if (!imported) {
        project_status_ = QStringLiteral("DAWproject not opened");
        project_detail_ = QString::fromStdString(error);
        emit projectStatusChanged();
        return false;
    }
    // A VST3 state arrives as a .vstpreset: loaded into the plugin, it is
    // kept as the state the plugin reports, which is what the song holds.
    for (const auto& preset : imported->presets) {
        auto* slot = blokkily::song_slot(imported->song, preset.where);
        if (slot == nullptr || slot->format != "VST3") continue;
        std::size_t index = 0;
        const auto found = blokkily::Vst3PluginInstance::scan(slot->path);
        for (std::size_t candidate = 0; candidate < found.size(); ++candidate)
            if (found[candidate].identifier == slot->identifier) index = candidate;
        std::string why;
        auto plugin = blokkily::Vst3PluginInstance::create(slot->path, index, &why);
        if (plugin && plugin->load_preset(preset.preset)) slot->state = plugin->save_state();
        else imported->omitted.push_back("The state of " + slot->identifier + " could not be loaded");
    }
    for (const auto& line : imported->omitted) dawproject_omitted_.append(QString::fromStdString(line));
    blokkily::Project project;
    project.song = std::move(imported->song);
    installProject(std::move(project), QString());
    if (transport_) transport_->rewind();
    project_status_ = QStringLiteral("Opened %1").arg(QFileInfo(file).fileName());
    project_detail_ = QString("%1 track%2 · %3 clip%4 · %5 note%6 · %7 device%8")
                          .arg(imported->tracks).arg(imported->tracks == 1 ? "" : "s")
                          .arg(imported->clips + imported->audio_clips)
                          .arg(imported->clips + imported->audio_clips == 1 ? "" : "s")
                          .arg(imported->notes).arg(imported->notes == 1 ? "" : "s")
                          .arg(imported->devices).arg(imported->devices == 1 ? "" : "s");
    if (!dawproject_omitted_.isEmpty())
        project_detail_ += QString(" · %1 left out: %2").arg(dawproject_omitted_.size()).arg(dawproject_omitted_.join("; "));
    emit projectStatusChanged();
    return true;
}

