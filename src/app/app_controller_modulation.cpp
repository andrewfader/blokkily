// What the modulation panel asks the running engine (phase 2, wave 5.1):
// which parameters the selected track's processors expose, and what they are
// called. The song keeps the modulators; the processors know their
// parameters, and those are read here, on the main thread.

#include "app_controller.hpp"

#include <QVariantMap>

#include <optional>

namespace {

std::optional<blokkily::ProcessorAddress> address_of(const QString& kind, int bus, int slot) {
    using blokkily::BusKind;
    if (bus < 0 || slot < -1) return std::nullopt;
    if (kind == "track") return blokkily::ProcessorAddress{BusKind::track, static_cast<std::uint32_t>(bus), slot};
    if (slot < 0) return std::nullopt;
    if (kind == "return") return blokkily::ProcessorAddress{BusKind::ret, static_cast<std::uint32_t>(bus), slot};
    if (kind == "master" && bus == 0) return blokkily::ProcessorAddress{BusKind::master, 0, slot};
    return std::nullopt;
}

} // namespace

QVariantList AppController::modulationTargets() const {
    QVariantList rows;
    if (!engine_ || song_ == nullptr) return rows;
    const int track = song_->selectedTrack();
    const auto& song = song_->song();
    if (track < 0 || static_cast<std::size_t>(track) >= song.tracks.size()) return rows;
    const auto inserts = static_cast<int>(song.tracks[static_cast<std::size_t>(track)].inserts.size());
    for (int slot = -1; slot < inserts; ++slot) {
        const blokkily::ProcessorAddress where{blokkily::BusKind::track,
                                               static_cast<std::uint32_t>(track), slot};
        const auto* instance = engine_->processor(where);
        if (instance == nullptr) continue;
        const QString place = slot < 0 ? QStringLiteral("INSTR") : QString("FX%1").arg(slot + 1);
        for (const auto& parameter : instance->parameters())
            if (parameter.automatable)
                rows.push_back(QVariantMap{
                    {"kind", QStringLiteral("track")},
                    {"bus", track},
                    {"slot", slot},
                    {"parameter", static_cast<int>(parameter.id)},
                    {"label", QString("%1 · %2").arg(place, QString::fromStdString(parameter.name))}});
    }
    return rows;
}

QString AppController::parameterName(const QString& kind, int bus, int slot, int parameter) const {
    const auto where = address_of(kind, bus, slot);
    const auto* instance = where && engine_ ? engine_->processor(*where) : nullptr;
    if (instance != nullptr)
        for (const auto& info : instance->parameters())
            if (info.id == parameter) return QString::fromStdString(info.name);
    return QString("P%1").arg(parameter);
}
