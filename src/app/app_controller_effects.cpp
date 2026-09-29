// The browser's effects half (item 2.4): which kind it lists, the built-in
// effects the application offers without a scan, and inserting a chosen
// effect on the rack's bus. The song model owns the chains; the rebuild that
// follows an insert adopts every processor already running.

#include "app_controller.hpp"
#include "app_controller_internal.hpp"

#include "blokkily/plugins/plugin_scan.hpp"

using app_detail::plugin_entry;

QVariantList AppController::plugins() const {
    QVariantList entries = plugins_;
    // What the application provides itself (the sampler's instruments, the
    // built-in effects) comes from the factory's registry and follows what
    // the scan found, so every scanned entry keeps its index, a browser entry
    // cannot drift from the processor it creates, and no scan finds them.
    for (const auto& entry : blokkily::internal_catalog())
        entries.push_back(plugin_entry(QString::fromStdString(entry.slot.format), entry.name,
                                       "Blokkily built-in", entry.slot.path, entry.slot.identifier, 0,
                                       entry.kind));
    return entries;
}

void AppController::setBrowserKind(const QString& kind) {
    const QString wanted = kind == QLatin1String(blokkily::effect_kind.data(),
                                                 blokkily::effect_kind.size())
                               ? QStringLiteral("effect")
                               : QStringLiteral("instrument");
    if (wanted == browser_kind_) return;
    browser_kind_ = wanted;
    emit browserChanged();
}

int AppController::browserTotal() const {
    int total = 0;
    for (const auto& entry : plugins())
        if (entry.toMap().value("kind").toString() == browser_kind_) ++total;
    return total;
}

bool AppController::addEffect(int index) {
    const auto entries = plugins();
    if (song_ == nullptr || index < 0 || index >= entries.size()) return false;
    const auto entry = entries.at(index).toMap();
    if (entry.value("kind").toString() != "effect") return false;
    blokkily::PluginSlot slot;
    slot.format = entry.value("format").toString().toStdString();
    slot.path = entry.value("path").toString().toStdString();
    slot.identifier = entry.value("identifier").toString().toStdString();
    if (!song_->addInsertToRack(slot)) return false;
    status_ = QString("Inserted %1 · latency %2 samples")
                  .arg(entry.value("name").toString())
                  .arg(outputLatency());
    emit statusChanged();
    return engine_ != nullptr;
}

void AppController::publishProcessorPorts() {
    if (song_ == nullptr) return;
    std::vector<blokkily::ProcessorAddress> keyable;
    std::vector<int> outputs(song_->song().tracks.size(), 0);
    if (engine_) {
        for (const auto& where : engine_->processor_addresses()) {
            const auto* processor = engine_->processor(where);
            if (processor == nullptr) continue;
            const auto ports = processor->ports();
            if (where.instrument()) {
                if (where.kind == blokkily::BusKind::track && where.bus < outputs.size())
                    outputs[where.bus] = static_cast<int>(ports.aux_outputs);
            } else if (ports.sidechain_inputs > 0) {
                keyable.push_back(where);
            }
        }
    }
    song_->setProcessorPorts(std::move(keyable), std::move(outputs));
}
