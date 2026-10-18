// DAWproject export (features/dawproject.feature): the song as the open
// exchange format other hosts read, with each running plugin's state as that
// host would load it.

#include "app_controller.hpp"
#include "song_model.hpp"

#include "blokkily/project/dawproject.hpp"

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
