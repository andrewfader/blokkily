// Effects in the song model (item 2.4): insert chains on tracks, returns and
// the master bus; return buses; per-track sends. Adding or removing an insert
// or a return changes the audio graph and is announced as a structure change
// (the engine is rebuilt, adopting what it already holds); bypass, send
// levels and the return strips are mixer moves that reach the running engine
// without a rebuild.

#include "song_model.hpp"

#include "blokkily/audio/mixer.hpp"
#include "blokkily/effects/builtin.hpp"

#include <QFileInfo>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

QString bus_kind_name(blokkily::BusKind kind) {
    switch (kind) {
    case blokkily::BusKind::track: return QStringLiteral("track");
    case blokkily::BusKind::ret: return QStringLiteral("return");
    case blokkily::BusKind::master: return QStringLiteral("master");
    }
    return QStringLiteral("track");
}

std::optional<blokkily::BusKind> bus_kind_from(const QString& name) {
    if (name == "track") return blokkily::BusKind::track;
    if (name == "return") return blokkily::BusKind::ret;
    if (name == "master") return blokkily::BusKind::master;
    return std::nullopt;
}

// What a rack row calls an effect the scan has not named: a built-in by its
// own name, a plugin by its file.
QString effect_label(const blokkily::PluginSlot& slot) {
    if (slot.format == blokkily::builtin_effect_format)
        for (const auto& effect : blokkily::builtin_effects())
            if (effect.identifier == slot.identifier) return QString::fromStdString(effect.name);
    const QString file = QFileInfo(QString::fromStdString(slot.path)).completeBaseName();
    return file.isEmpty() ? QString::fromStdString(slot.identifier) : file;
}

QString format_badge(const blokkily::PluginSlot& slot) {
    if (slot.format == blokkily::builtin_effect_format) return QStringLiteral("INT");
    if (slot.format == "SoundFont") return QStringLiteral("SF");
    return QString::fromStdString(slot.format);
}

QString decibels(double value) {
    if (value <= blokkily::minimum_audible_db) return QStringLiteral("-inf");
    return QString("%1%2").arg(value > 0.0 ? "+" : "").arg(value, 0, 'f', 1);
}

// A return is named by a letter, the way a console labels its aux buses.
QString return_letter(std::size_t index) {
    return index < 26 ? QString(QChar('A' + static_cast<int>(index)))
                      : QString::number(index + 1);
}

} // namespace

std::vector<blokkily::EffectSlot>* SongModel::chainAt(blokkily::BusKind kind, int bus) {
    switch (kind) {
    case blokkily::BusKind::track:
        return validTrack(bus) ? &song_.tracks[static_cast<std::size_t>(bus)].inserts : nullptr;
    case blokkily::BusKind::ret:
        return bus >= 0 && static_cast<std::size_t>(bus) < song_.returns.size()
                   ? &song_.returns[static_cast<std::size_t>(bus)].inserts
                   : nullptr;
    case blokkily::BusKind::master:
        return bus == 0 ? &song_.master_inserts : nullptr;
    }
    return nullptr;
}

blokkily::ProcessorAddress SongModel::rackBus() const {
    // A rack left pointing at a return that undo took away falls back to the
    // selected track rather than at nothing.
    if (rack_kind_ == blokkily::BusKind::ret &&
        static_cast<std::size_t>(rack_return_) < song_.returns.size())
        return {blokkily::BusKind::ret, static_cast<std::uint32_t>(rack_return_), 0};
    if (rack_kind_ == blokkily::BusKind::master) return {blokkily::BusKind::master, 0, 0};
    return {blokkily::BusKind::track, static_cast<std::uint32_t>(selected_track_), 0};
}

QVariantMap SongModel::rack() const {
    const auto where = rackBus();
    const std::vector<blokkily::EffectSlot>* chain = nullptr;
    QString title;
    switch (where.kind) {
    case blokkily::BusKind::track:
        chain = &song_.tracks[where.bus].inserts;
        title = QString::fromStdString(song_.tracks[where.bus].name);
        break;
    case blokkily::BusKind::ret:
        chain = &song_.returns[where.bus].inserts;
        title = QString("RETURN %1 · %2")
                    .arg(return_letter(where.bus),
                         QString::fromStdString(song_.returns[where.bus].name));
        break;
    case blokkily::BusKind::master:
        chain = &song_.master_inserts;
        title = QStringLiteral("MASTER");
        break;
    }
    QVariantList inserts;
    for (std::size_t index = 0; index < chain->size(); ++index) {
        const auto& slot = (*chain)[index];
        // A key (wave 5.2): offered on the inserts that listen to one - the
        // built-in compressor, and a plugin that declares a sidechain input.
        const int key = slot.sidechain ? static_cast<int>(*slot.sidechain) : -1;
        const blokkily::ProcessorAddress address{where.kind, where.bus,
                                                 static_cast<std::int32_t>(index)};
        const bool keyable = (slot.plugin.format == blokkily::builtin_effect_format &&
                              slot.plugin.identifier == "compressor") ||
                             std::find(keyable_inserts_.begin(), keyable_inserts_.end(),
                                       address) != keyable_inserts_.end() ||
                             key >= 0;
        inserts.push_back(QVariantMap{
            {"index", static_cast<int>(index)},
            {"name", effectLabel(slot.plugin)},
            {"format", format_badge(slot.plugin)},
            {"bypass", slot.bypass},
            {"keyable", keyable},
            {"sidechain", key},
            {"sidechainName", key >= 0 && static_cast<std::size_t>(key) < song_.tracks.size()
                                  ? QString::fromStdString(song_.tracks[static_cast<std::size_t>(key)].name)
                                  : QString()}});
    }
    return {{"kind", bus_kind_name(where.kind)},
            {"bus", static_cast<int>(where.bus)},
            {"title", title},
            {"inserts", inserts}};
}

namespace {
std::string plugin_key(const std::string& format, const std::string& path,
                       const std::string& identifier) {
    return format + '\n' + path + '\n' + identifier;
}
} // namespace

void SongModel::learnPluginNames(const QVariantList& plugins) {
    bool learned = false;
    for (const auto& entry : plugins) {
        const auto row = entry.toMap();
        const auto name = row.value("name").toString();
        const auto path = row.value("path").toString().toStdString();
        if (name.isEmpty() || path.empty()) continue;
        const auto format = row.value("format").toString().toStdString();
        const auto identifier = row.value("identifier").toString().toStdString();
        for (const auto& key : {plugin_key(format, path, identifier), plugin_key(format, path, {})}) {
            auto& known = plugin_names_[key];
            learned = learned || known != name;
            known = name;
        }
    }
    if (learned) emit songChanged();
}

QString SongModel::effectLabel(const blokkily::PluginSlot& slot) const {
    if (slot.format != blokkily::builtin_effect_format) {
        auto found = plugin_names_.find(plugin_key(slot.format, slot.path, slot.identifier));
        if (found == plugin_names_.end())
            found = plugin_names_.find(plugin_key(slot.format, slot.path, {}));
        if (found != plugin_names_.end()) return found->second;
    }
    return effect_label(slot);
}

void SongModel::setProcessorPorts(std::vector<blokkily::ProcessorAddress> keyable,
                                  std::vector<int> instrument_outputs) {
    if (keyable == keyable_inserts_ && instrument_outputs == instrument_outputs_) return;
    keyable_inserts_ = std::move(keyable);
    instrument_outputs_ = std::move(instrument_outputs);
    emit songChanged();
}

int SongModel::instrumentOutputs(int track) const {
    if (track < 0 || static_cast<std::size_t>(track) >= instrument_outputs_.size()) return 0;
    return instrument_outputs_[static_cast<std::size_t>(track)];
}

QVariantList SongModel::returns() const {
    QVariantList rows;
    for (std::size_t index = 0; index < song_.returns.size(); ++index) {
        const auto& bus = song_.returns[index];
        const double peak =
            index < return_peaks_.size() ? static_cast<double>(return_peaks_[index]) : 0.0;
        rows.push_back(QVariantMap{
            {"index", static_cast<int>(index)},
            {"letter", return_letter(index)},
            {"name", QString::fromStdString(bus.name)},
            {"gainDb", bus.mix.gain_db},
            {"gainText", decibels(bus.mix.gain_db)},
            {"pan", bus.mix.pan},
            {"mute", bus.mix.mute},
            {"inserts", static_cast<int>(bus.inserts.size())},
            {"racked", rack_kind_ == blokkily::BusKind::ret &&
                           rack_return_ == static_cast<int>(index)},
            {"peakFraction",
             peak <= 0.0 ? 0.0
                         : qBound(0.0, (blokkily::linear_to_db(peak) + 60.0) / 60.0, 1.0)}});
    }
    return rows;
}

QVariantList SongModel::sendsOf(std::size_t track) const {
    QVariantList sends;
    for (std::size_t bus = 0; bus < song_.returns.size(); ++bus) {
        double level = blokkily::minimum_audible_db;
        bool pre = false;
        bool active = false;
        for (const auto& send : song_.tracks[track].sends)
            if (send.bus == bus) {
                level = send.level_db;
                pre = send.pre_fader;
                active = true;
            }
        sends.push_back(QVariantMap{{"bus", static_cast<int>(bus)},
                                    {"letter", return_letter(bus)},
                                    {"levelDb", std::max(level, -60.0)},
                                    {"levelText", active ? decibels(level) : QStringLiteral("off")},
                                    {"pre", pre},
                                    {"active", active}});
    }
    return sends;
}

bool SongModel::masterRacked() const { return rack_kind_ == blokkily::BusKind::master; }

void SongModel::selectRack(const QString& kind, int bus) {
    const auto parsed = bus_kind_from(kind);
    if (!parsed) return;
    switch (*parsed) {
    case blokkily::BusKind::track:
        if (!validTrack(bus)) return;
        rack_kind_ = blokkily::BusKind::track;
        selected_track_ = bus;
        break;
    case blokkily::BusKind::ret:
        if (bus < 0 || static_cast<std::size_t>(bus) >= song_.returns.size()) return;
        rack_kind_ = blokkily::BusKind::ret;
        rack_return_ = bus;
        break;
    case blokkily::BusKind::master:
        rack_kind_ = blokkily::BusKind::master;
        break;
    }
    emit songChanged();
}

bool SongModel::addInsert(blokkily::BusKind kind, int bus, const blokkily::PluginSlot& plugin) {
    if (plugin.format.empty()) return false;
    auto* chain = chainAt(kind, bus);
    if (chain == nullptr) return false;
    checkpoint();
    chain->push_back({plugin, false, {}});
    notifyStructureChanged();
    return true;
}

bool SongModel::addInsertToRack(const blokkily::PluginSlot& plugin) {
    const auto where = rackBus();
    return addInsert(where.kind, static_cast<int>(where.bus), plugin);
}

bool SongModel::removeInsert(const QString& kind, int bus, int slot) {
    const auto parsed = bus_kind_from(kind);
    if (!parsed) return false;
    auto* chain = chainAt(*parsed, bus);
    if (chain == nullptr || slot < 0 || static_cast<std::size_t>(slot) >= chain->size())
        return false;
    checkpoint();
    // Automation and modulation that drove the removed effect go with it;
    // what drives the effects after it follows them to their new slots.
    (void)song_.remove_insert(*parsed, static_cast<std::size_t>(bus),
                              static_cast<std::size_t>(slot));
    notifyStructureChanged();
    return true;
}

bool SongModel::setInsertBypass(const QString& kind, int bus, int slot, bool bypass) {
    const auto parsed = bus_kind_from(kind);
    if (!parsed) return false;
    auto* chain = chainAt(*parsed, bus);
    if (chain == nullptr || slot < 0 || static_cast<std::size_t>(slot) >= chain->size())
        return false;
    auto& target = (*chain)[static_cast<std::size_t>(slot)];
    if (target.bypass == bypass) return true;
    checkpoint();
    target.bypass = bypass;
    // A bypassed slot keeps its plugin and its latency: a mixer move.
    emit mixChanged();
    emit songChanged();
    return true;
}

int SongModel::addReturn() {
    checkpoint();
    blokkily::ReturnBus bus;
    bus.name = QString("RETURN %1").arg(return_letter(song_.returns.size())).toStdString();
    song_.returns.push_back(std::move(bus));
    return_peaks_.resize(song_.returns.size(), 0.0F);
    notifyStructureChanged();
    return static_cast<int>(song_.returns.size()) - 1;
}

bool SongModel::removeReturn(int bus) {
    if (bus < 0 || static_cast<std::size_t>(bus) >= song_.returns.size()) return false;
    checkpoint();
    const auto removed = static_cast<std::size_t>(bus);
    (void)song_.remove_return(removed);
    if (removed < return_peaks_.size())
        return_peaks_.erase(return_peaks_.begin() + static_cast<std::ptrdiff_t>(removed));
    if (rack_kind_ == blokkily::BusKind::ret && rack_return_ >= bus)
        rack_kind_ = blokkily::BusKind::track;
    notifyStructureChanged();
    return true;
}

blokkily::Send* SongModel::sendAt(int track, int bus, bool create) {
    if (!validTrack(track) || bus < 0 || static_cast<std::size_t>(bus) >= song_.returns.size())
        return nullptr;
    auto& sends = song_.tracks[static_cast<std::size_t>(track)].sends;
    for (auto& send : sends)
        if (send.bus == static_cast<std::size_t>(bus)) return &send;
    if (!create) return nullptr;
    sends.push_back({static_cast<std::size_t>(bus), blokkily::minimum_audible_db, false});
    return &sends.back();
}

void SongModel::setSendLevel(int track, int bus, double decibels_value) {
    if (sendAt(track, bus, false) == nullptr &&
        (!validTrack(track) || bus < 0 || static_cast<std::size_t>(bus) >= song_.returns.size()))
        return;
    checkpoint(QString("send:%1:%2").arg(track).arg(bus));
    auto* send = sendAt(track, bus, true);
    send->level_db = qBound(blokkily::minimum_audible_db, decibels_value, 6.0);
    emit mixChanged();
}

void SongModel::setSendPreFader(int track, int bus, bool pre) {
    auto* existing = sendAt(track, bus, false);
    if (existing != nullptr && existing->pre_fader == pre) return;
    if (existing == nullptr &&
        (!validTrack(track) || bus < 0 || static_cast<std::size_t>(bus) >= song_.returns.size()))
        return;
    checkpoint();
    sendAt(track, bus, true)->pre_fader = pre;
    emit mixChanged();
}

void SongModel::setReturnGain(int bus, double decibels_value) {
    if (bus < 0 || static_cast<std::size_t>(bus) >= song_.returns.size()) return;
    checkpoint(QString("return-gain:%1").arg(bus));
    song_.returns[static_cast<std::size_t>(bus)].mix.gain_db =
        qBound(blokkily::minimum_audible_db, decibels_value, 6.0);
    emit mixChanged();
}

void SongModel::setReturnPan(int bus, double pan) {
    if (bus < 0 || static_cast<std::size_t>(bus) >= song_.returns.size()) return;
    checkpoint(QString("return-pan:%1").arg(bus));
    song_.returns[static_cast<std::size_t>(bus)].mix.pan = qBound(-1.0, pan, 1.0);
    emit mixChanged();
}

void SongModel::toggleReturnMute(int bus) {
    if (bus < 0 || static_cast<std::size_t>(bus) >= song_.returns.size()) return;
    checkpoint();
    auto& mix = song_.returns[static_cast<std::size_t>(bus)].mix;
    mix.mute = !mix.mute;
    emit mixChanged();
}

QVariantList SongModel::sendLevels() const {
    QVariantList tracks;
    for (std::size_t track = 0; track < song_.tracks.size(); ++track)
        tracks.push_back(sendsOf(track));
    return tracks;
}

QVariantList SongModel::returnMix() const {
    QVariantList rows;
    for (const auto& bus : song_.returns)
        rows.push_back(QVariantMap{{"gainDb", bus.mix.gain_db},
                                   {"gainText", decibels(bus.mix.gain_db)},
                                   {"pan", bus.mix.pan},
                                   {"mute", bus.mix.mute}});
    return rows;
}

QVariantList SongModel::returnMeters() const {
    QVariantList fractions;
    for (std::size_t index = 0; index < song_.returns.size(); ++index) {
        const double peak =
            index < return_peaks_.size() ? static_cast<double>(return_peaks_[index]) : 0.0;
        fractions.push_back(
            peak <= 0.0 ? 0.0
                        : qBound(0.0, (blokkily::linear_to_db(peak) + 60.0) / 60.0, 1.0));
    }
    return fractions;
}

void SongModel::setReturnMeters(const std::vector<float>& peaks) {
    return_peaks_ = peaks;
    return_peaks_.resize(song_.returns.size(), 0.0F);
    emit metersChanged();
}
