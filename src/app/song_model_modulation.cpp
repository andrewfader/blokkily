// Modulators and sidechain keys in the song model (phase 2, waves 5.1 and
// 5.2). They are part of the song: every edit is a step of history and is
// saved with the project. What changes the routing - a modulator added or
// removed, a target, a follower's source, an insert's key - is announced as a
// structure change, which the running engine takes as a recompile, never a
// rebuild. A rate, a shape, a macro or a depth is a live move announced as a
// mix change, which reaches the engine the way a fader does.

#include "song_model.hpp"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

constexpr const char* shape_names[] = {"SINE", "TRIANGLE", "SAW UP", "SAW DOWN", "SQUARE",
                                       "RANDOM"};

std::optional<blokkily::BusKind> bus_kind(const QString& name) {
    if (name == "track") return blokkily::BusKind::track;
    if (name == "return") return blokkily::BusKind::ret;
    if (name == "master") return blokkily::BusKind::master;
    return std::nullopt;
}

QString bus_kind_name(blokkily::BusKind kind) {
    switch (kind) {
    case blokkily::BusKind::track: return QStringLiteral("track");
    case blokkily::BusKind::ret: return QStringLiteral("return");
    case blokkily::BusKind::master: return QStringLiteral("master");
    }
    return QStringLiteral("track");
}

QString kind_name(blokkily::Modulator::Kind kind) {
    switch (kind) {
    case blokkily::Modulator::Kind::lfo: return QStringLiteral("lfo");
    case blokkily::Modulator::Kind::macro: return QStringLiteral("macro");
    case blokkily::Modulator::Kind::follower: return QStringLiteral("follower");
    }
    return QStringLiteral("lfo");
}

// Where a target sits, the way the mixer names it: "BASS · INSTR", "BASS ·
// FX2", "RETURN A · FX1", "MASTER · FX1".
QString place_of(const blokkily::Song& song, const blokkily::ProcessorAddress& where) {
    QString bus;
    switch (where.kind) {
    case blokkily::BusKind::track:
        bus = where.bus < song.tracks.size() ? QString::fromStdString(song.tracks[where.bus].name)
                                             : QStringLiteral("TRACK");
        break;
    case blokkily::BusKind::ret:
        bus = QString("RETURN %1").arg(where.bus < 26 ? QString(QChar('A' + static_cast<int>(where.bus)))
                                                        : QString::number(where.bus + 1));
        break;
    case blokkily::BusKind::master: bus = QStringLiteral("MASTER"); break;
    }
    return QString("%1 · %2").arg(bus, where.slot < 0 ? QStringLiteral("INSTR")
                                                      : QString("FX%1").arg(where.slot + 1));
}

// The name a new modulator gets: its kind and the first number not taken.
std::string fresh_name(const blokkily::Song& song, const char* stem) {
    for (int number = 1;; ++number) {
        const std::string name = std::string(stem) + " " + std::to_string(number);
        const bool taken = std::any_of(song.modulators.begin(), song.modulators.end(),
                                       [&](const blokkily::Modulator& m) { return m.name == name; });
        if (!taken) return name;
    }
}

} // namespace

blokkily::Modulator* SongModel::modulatorAt(int modulator) {
    if (modulator < 0 || static_cast<std::size_t>(modulator) >= song_.modulators.size())
        return nullptr;
    return &song_.modulators[static_cast<std::size_t>(modulator)];
}

QStringList SongModel::lfoShapes() const {
    QStringList names;
    for (const auto* name : shape_names) names.push_back(QString::fromLatin1(name));
    return names;
}

QVariantList SongModel::modulators() const {
    QVariantList rows;
    for (std::size_t index = 0; index < song_.modulators.size(); ++index) {
        const auto& modulator = song_.modulators[index];
        QVariantList targets;
        for (std::size_t t = 0; t < modulator.targets.size(); ++t) {
            const auto& target = modulator.targets[t];
            targets.push_back(QVariantMap{
                {"index", static_cast<int>(t)},
                {"kind", bus_kind_name(target.processor.kind)},
                {"bus", static_cast<int>(target.processor.bus)},
                {"slot", target.processor.slot},
                {"parameter", target.parameter_index},
                {"place", place_of(song_, target.processor)},
                {"depth", target.depth},
                {"depthText", QString("%1%2%").arg(target.depth > 0 ? "+" : "")
                                  .arg(qRound(target.depth * 100.0))}});
        }
        const auto source = modulator.source_track;
        rows.push_back(QVariantMap{
            {"index", static_cast<int>(index)},
            {"kind", kind_name(modulator.kind)},
            {"name", QString::fromStdString(modulator.name)},
            {"shape", QString::fromLatin1(shape_names[static_cast<std::size_t>(modulator.shape)])},
            {"rateHz", modulator.rate_hz},
            {"rateText", QString("%1 Hz").arg(modulator.rate_hz, 0, 'f',
                                              modulator.rate_hz < 10.0 ? 2 : 1)},
            {"value", modulator.value},
            {"valueText", QString("%1%").arg(qRound(modulator.value * 100.0))},
            {"sourceTrack", static_cast<int>(source)},
            {"sourceName", source < song_.tracks.size()
                               ? QString::fromStdString(song_.tracks[source].name)
                               : QString()},
            {"targets", targets}});
    }
    return rows;
}

int SongModel::addModulator(const QString& kind) {
    if (song_.modulators.size() >= blokkily::maximum_modulators) return -1;
    blokkily::Modulator modulator;
    if (kind == "lfo") {
        modulator.kind = blokkily::Modulator::Kind::lfo;
        modulator.name = fresh_name(song_, "LFO");
    } else if (kind == "macro") {
        modulator.kind = blokkily::Modulator::Kind::macro;
        modulator.name = fresh_name(song_, "MACRO");
    } else if (kind == "follower") {
        modulator.kind = blokkily::Modulator::Kind::follower;
        modulator.name = fresh_name(song_, "FOLLOW");
        modulator.source_track = static_cast<std::uint32_t>(selected_track_);
    } else {
        return -1;
    }
    checkpoint();
    song_.modulators.push_back(std::move(modulator));
    notifyStructureChanged();
    return static_cast<int>(song_.modulators.size()) - 1;
}

bool SongModel::removeModulator(int modulator) {
    if (modulatorAt(modulator) == nullptr) return false;
    checkpoint();
    song_.modulators.erase(song_.modulators.begin() + modulator);
    notifyStructureChanged();
    return true;
}

bool SongModel::addModulationTarget(int modulator, const QString& kind, int bus, int slot,
                                    int parameter) {
    auto* found = modulatorAt(modulator);
    const auto parsed = bus_kind(kind);
    if (found == nullptr || !parsed || bus < 0 || slot < -1 || parameter < 0 ||
        found->targets.size() >= blokkily::Modulator::maximum_targets)
        return false;
    const blokkily::ProcessorAddress where{*parsed, static_cast<std::uint32_t>(bus), slot};
    for (const auto& target : found->targets)
        if (target.processor == where && target.parameter_index == parameter) return true;
    // Checked on a copy first, so a target the song cannot hold changes
    // nothing and takes no step of history.
    auto trial = song_;
    trial.modulators[static_cast<std::size_t>(modulator)].targets.push_back({where, parameter, 0.5});
    if (!trial.consistent()) return false;
    checkpoint();
    song_ = std::move(trial);
    notifyStructureChanged();
    return true;
}

bool SongModel::removeModulationTarget(int modulator, int target) {
    auto* found = modulatorAt(modulator);
    if (found == nullptr || target < 0 || static_cast<std::size_t>(target) >= found->targets.size())
        return false;
    checkpoint();
    found->targets.erase(found->targets.begin() + target);
    notifyStructureChanged();
    return true;
}

bool SongModel::setFollowerSource(int modulator, int track) {
    auto* found = modulatorAt(modulator);
    if (found == nullptr || found->kind != blokkily::Modulator::Kind::follower ||
        !validTrack(track))
        return false;
    if (found->source_track == static_cast<std::uint32_t>(track)) return true;
    checkpoint();
    found->source_track = static_cast<std::uint32_t>(track);
    notifyStructureChanged();
    return true;
}

void SongModel::setModulatorShape(int modulator, const QString& shape) {
    auto* found = modulatorAt(modulator);
    if (found == nullptr) return;
    for (std::size_t index = 0; index < std::size(shape_names); ++index) {
        if (shape != QLatin1String(shape_names[index])) continue;
        const auto wanted = static_cast<blokkily::LfoShape>(index);
        if (found->shape == wanted) return;
        checkpoint();
        found->shape = wanted;
        emit mixChanged();
        emit modulationChanged();
        return;
    }
}

void SongModel::setModulatorRate(int modulator, double hertz) {
    auto* found = modulatorAt(modulator);
    if (found == nullptr || !std::isfinite(hertz)) return;
    checkpoint(QString("mod-rate:%1").arg(modulator));
    found->rate_hz = qBound(blokkily::Modulator::minimum_rate_hz, hertz,
                            blokkily::Modulator::maximum_rate_hz);
    emit mixChanged();
    emit modulationChanged();
}

void SongModel::setModulatorValue(int modulator, double value) {
    auto* found = modulatorAt(modulator);
    if (found == nullptr || !std::isfinite(value)) return;
    checkpoint(QString("mod-value:%1").arg(modulator));
    found->value = qBound(0.0, value, 1.0);
    emit mixChanged();
    emit modulationChanged();
}

void SongModel::setModulationDepth(int modulator, int target, double depth) {
    auto* found = modulatorAt(modulator);
    if (found == nullptr || target < 0 ||
        static_cast<std::size_t>(target) >= found->targets.size() || !std::isfinite(depth))
        return;
    checkpoint(QString("mod-depth:%1:%2").arg(modulator).arg(target));
    found->targets[static_cast<std::size_t>(target)].depth = qBound(-1.0, depth, 1.0);
    emit mixChanged();
    emit modulationChanged();
}

bool SongModel::setInsertSidechain(const QString& kind, int bus, int slot, int track) {
    const auto parsed = bus_kind(kind);
    if (!parsed) return false;
    auto* chain = chainAt(*parsed, bus);
    if (chain == nullptr || slot < 0 || static_cast<std::size_t>(slot) >= chain->size())
        return false;
    std::optional<std::uint32_t> key;
    if (track >= 0) {
        if (!validTrack(track)) return false;
        key = static_cast<std::uint32_t>(track);
    }
    if ((*chain)[static_cast<std::size_t>(slot)].sidechain == key) return true;
    // A key from its own track, or one that closes a loop, is refused before
    // it takes a step of history.
    auto trial = song_;
    if (auto* trial_chain = [&]() -> std::vector<blokkily::EffectSlot>* {
            switch (*parsed) {
            case blokkily::BusKind::track: return &trial.tracks[static_cast<std::size_t>(bus)].inserts;
            case blokkily::BusKind::ret: return &trial.returns[static_cast<std::size_t>(bus)].inserts;
            case blokkily::BusKind::master: return &trial.master_inserts;
            }
            return nullptr;
        }())
        (*trial_chain)[static_cast<std::size_t>(slot)].sidechain = key;
    if (!trial.consistent()) return false;
    checkpoint();
    song_ = std::move(trial);
    notifyStructureChanged();
    return true;
}

int SongModel::addInstrumentOutput(int track, int output) {
    if (!validTrack(track) || output < 1 || output > instrumentOutputs(track)) return -1;
    const auto& source = song_.tracks[static_cast<std::size_t>(track)];
    if (source.instrument.format.empty()) return -1;
    const blokkily::InstrumentOutput wanted{static_cast<std::uint32_t>(track),
                                            static_cast<std::uint32_t>(output)};
    for (std::size_t index = 0; index < song_.tracks.size(); ++index)
        if (song_.tracks[index].source == wanted) {
            selectTrack(static_cast<int>(index));
            return static_cast<int>(index);
        }
    blokkily::Track channel;
    channel.name = QString("%1 AUX %2").arg(QString::fromStdString(source.name)).arg(output)
                       .left(24).toStdString();
    channel.source = wanted;
    auto trial = song_;
    trial.tracks.push_back(channel);
    if (!trial.consistent()) return -1;
    checkpoint();
    song_ = std::move(trial);
    peaks_.push_back(0.0F);
    selected_track_ = static_cast<int>(song_.tracks.size()) - 1;
    rack_kind_ = blokkily::BusKind::track;
    notifyStructureChanged();
    return selected_track_;
}
