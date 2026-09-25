// Automation lanes and modes as the interface edits them (item 3.1; decision
// 3). A lane is part of what the song plays, so every edit is a step of
// history and reaches the running engine as a recompile, never a rebuild.

#include "song_model.hpp"

#include "blokkily/audio/mixer.hpp"

#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace {

using blokkily::AutomationMode;
using blokkily::AutomationTarget;

constexpr std::array<const char*, 5> mode_names{"OFF", "READ", "TOUCH", "LATCH", "WRITE"};

QString lane_name(const AutomationTarget& target) {
    switch (target.kind) {
    case AutomationTarget::Kind::gain: return QStringLiteral("GAIN");
    case AutomationTarget::Kind::pan: return QStringLiteral("PAN");
    case AutomationTarget::Kind::mute: return QStringLiteral("MUTE");
    case AutomationTarget::Kind::parameter: break;
    }
    const auto& where = target.processor;
    QString bus;
    switch (where.kind) {
    case blokkily::BusKind::track: bus = QString(); break;
    case blokkily::BusKind::ret: bus = QStringLiteral("RET %1 ").arg(QChar('A' + static_cast<int>(where.bus % 26))); break;
    case blokkily::BusKind::master: bus = QStringLiteral("MST "); break;
    }
    const QString slot = where.instrument() ? QStringLiteral("INST")
                                            : QStringLiteral("FX%1").arg(where.slot + 1);
    return QStringLiteral("%1%2 P%3").arg(bus, slot).arg(target.parameter_index);
}

// The range a lane's values are drawn and kept in.
std::pair<double, double> lane_range(const blokkily::AutomationLane& lane) {
    switch (lane.target.kind) {
    case AutomationTarget::Kind::gain: return {-60.0, 6.0};
    case AutomationTarget::Kind::pan: return {-1.0, 1.0};
    case AutomationTarget::Kind::mute: return {0.0, 1.0};
    case AutomationTarget::Kind::parameter: break;
    }
    double low = 0.0;
    double high = 1.0;
    for (const auto& point : lane.points) {
        low = std::min(low, point.value);
        high = std::max(high, point.value);
    }
    return {low, high};
}

double keep_in_range(const blokkily::AutomationLane& lane, double value) {
    if (!std::isfinite(value)) return 0.0;
    switch (lane.target.kind) {
    case AutomationTarget::Kind::gain: return qBound(blokkily::minimum_audible_db, value, 6.0);
    case AutomationTarget::Kind::pan: return qBound(-1.0, value, 1.0);
    case AutomationTarget::Kind::mute: return value >= 0.5 ? 1.0 : 0.0;
    case AutomationTarget::Kind::parameter: break;
    }
    return value;
}

// How many points of `points` sit on `tick`.
std::size_t on_tick(const std::vector<blokkily::AutomationPoint>& points, blokkily::Tick tick) {
    return static_cast<std::size_t>(std::count_if(
        points.begin(), points.end(),
        [tick](const blokkily::AutomationPoint& point) { return point.at == tick; }));
}

} // namespace

QStringList SongModel::automationModes() const {
    QStringList modes;
    for (const auto* name : mode_names) modes << QString::fromLatin1(name);
    return modes;
}

QString SongModel::automationMode(int track) const {
    if (!validTrack(track)) return {};
    return QString::fromLatin1(
        mode_names.at(static_cast<std::size_t>(song_.tracks[static_cast<std::size_t>(track)]
                                                   .automation_mode)));
}

bool SongModel::captures(int track) const {
    if (!validTrack(track)) return false;
    const auto mode = song_.tracks[static_cast<std::size_t>(track)].automation_mode;
    return mode == AutomationMode::touch || mode == AutomationMode::latch ||
           mode == AutomationMode::write;
}

bool SongModel::setAutomationMode(int track, const QString& mode) {
    if (!validTrack(track)) return false;
    const auto wanted = mode.trimmed().toUpper();
    for (std::size_t index = 0; index < mode_names.size(); ++index) {
        if (wanted != QLatin1String(mode_names[index])) continue;
        auto& current = song_.tracks[static_cast<std::size_t>(track)].automation_mode;
        const auto next = static_cast<AutomationMode>(index);
        if (current == next) return true;
        checkpoint();
        current = next;
        notifyStructureChanged();
        return true;
    }
    return false;
}

void SongModel::cycleAutomationMode(int track, int step) {
    if (!validTrack(track)) return;
    const auto count = static_cast<int>(mode_names.size());
    const auto current =
        static_cast<int>(song_.tracks[static_cast<std::size_t>(track)].automation_mode);
    const auto next = ((current + step) % count + count) % count;
    (void)setAutomationMode(track, QString::fromLatin1(mode_names.at(static_cast<std::size_t>(next))));
}

int SongModel::selectedLane() const {
    if (!validTrack(selected_track_)) return -1;
    const auto count =
        static_cast<int>(song_.tracks[static_cast<std::size_t>(selected_track_)].automation.size());
    if (count == 0) return -1;
    return qBound(0, selected_lane_, count - 1);
}

void SongModel::selectLane(int lane) {
    if (lane == selected_lane_) return;
    selected_lane_ = std::max(0, lane);
    emit songChanged();
}

QVariantList SongModel::automationLanes() const {
    QVariantList lanes;
    if (!validTrack(selected_track_)) return lanes;
    const auto& track = song_.tracks[static_cast<std::size_t>(selected_track_)];
    const int selected = selectedLane();
    for (std::size_t index = 0; index < track.automation.size(); ++index) {
        const auto& lane = track.automation[index];
        const auto [low, high] = lane_range(lane);
        QVariantList points;
        for (const auto& point : lane.points)
            points.push_back(QVariantMap{{"at", static_cast<double>(point.at)},
                                         {"value", point.value}});
        lanes.push_back(QVariantMap{
            {"index", static_cast<int>(index)},
            {"name", lane_name(lane.target)},
            {"kind", static_cast<int>(lane.target.kind)},
            {"minimum", low},
            {"maximum", high},
            {"points", points},
            {"selected", static_cast<int>(index) == selected},
        });
    }
    return lanes;
}

int SongModel::addAutomationLane(int track, const QString& kind) {
    if (!validTrack(track)) return -1;
    const auto wanted = kind.trimmed().toUpper();
    AutomationTarget target;
    target.processor = blokkily::track_instrument(static_cast<std::uint32_t>(track));
    auto& strip = song_.tracks[static_cast<std::size_t>(track)];
    double value = 0.0;
    if (wanted == "GAIN") {
        target.kind = AutomationTarget::Kind::gain;
        value = strip.mix.gain_db;
    } else if (wanted == "PAN") {
        target.kind = AutomationTarget::Kind::pan;
        value = strip.mix.pan;
    } else if (wanted == "MUTE") {
        target.kind = AutomationTarget::Kind::mute;
        value = strip.mix.mute ? 1.0 : 0.0;
    } else {
        return -1;
    }
    for (std::size_t index = 0; index < strip.automation.size(); ++index)
        if (strip.automation[index].target.kind == target.kind &&
            strip.automation[index].target.processor == target.processor)
            return static_cast<int>(index);
    checkpoint();
    strip.automation.push_back({target, {{0, value}}});
    if (track == selected_track_) selected_lane_ = static_cast<int>(strip.automation.size()) - 1;
    notifyStructureChanged();
    return static_cast<int>(strip.automation.size()) - 1;
}

bool SongModel::removeAutomationLane(int track, int lane) {
    if (!validTrack(track)) return false;
    auto& lanes = song_.tracks[static_cast<std::size_t>(track)].automation;
    if (lane < 0 || lane >= static_cast<int>(lanes.size())) return false;
    checkpoint();
    lanes.erase(lanes.begin() + lane);
    notifyStructureChanged();
    return true;
}

int SongModel::addAutomationPoint(int track, int lane, double tick, double value) {
    if (!validTrack(track) || !std::isfinite(tick)) return -1;
    auto& lanes = song_.tracks[static_cast<std::size_t>(track)].automation;
    if (lane < 0 || lane >= static_cast<int>(lanes.size())) return -1;
    auto& points = lanes[static_cast<std::size_t>(lane)].points;
    const auto at = static_cast<blokkily::Tick>(std::llround(std::max(0.0, tick)));
    if (on_tick(points, at) >= 2) return -1;
    checkpoint(QStringLiteral("automation:%1:%2").arg(track).arg(lane));
    const auto position = std::upper_bound(
        points.begin(), points.end(), at,
        [](blokkily::Tick tick_at, const blokkily::AutomationPoint& point) {
            return tick_at < point.at;
        });
    const auto inserted = points.insert(
        position, {at, keep_in_range(lanes[static_cast<std::size_t>(lane)], value)});
    notifyStructureChanged();
    return static_cast<int>(inserted - points.begin());
}

int SongModel::moveAutomationPoint(int track, int lane, int point, double tick, double value) {
    if (!validTrack(track) || !std::isfinite(tick)) return -1;
    auto& lanes = song_.tracks[static_cast<std::size_t>(track)].automation;
    if (lane < 0 || lane >= static_cast<int>(lanes.size())) return -1;
    auto& edited = lanes[static_cast<std::size_t>(lane)];
    auto& points = edited.points;
    if (point < 0 || point >= static_cast<int>(points.size())) return -1;
    const auto index = static_cast<std::size_t>(point);
    // Between its neighbours, and never a third point on one tick.
    blokkily::Tick low = 0;
    blokkily::Tick high = std::numeric_limits<blokkily::Tick>::max();
    if (index > 0) {
        low = points[index - 1].at;
        if (index > 1 && points[index - 2].at == low) ++low;
    }
    if (index + 1 < points.size()) {
        high = points[index + 1].at;
        if (index + 2 < points.size() && points[index + 2].at == high) --high;
    }
    auto at = static_cast<blokkily::Tick>(std::llround(std::max(0.0, tick)));
    at = low <= high ? std::clamp(at, low, high) : points[index].at;
    const double kept = keep_in_range(edited, value);
    if (points[index].at == at && points[index].value == kept) return point;
    checkpoint(QStringLiteral("automation:%1:%2").arg(track).arg(lane));
    points[index] = {at, kept};
    notifyStructureChanged();
    return point;
}

bool SongModel::removeAutomationPoint(int track, int lane, int point) {
    if (!validTrack(track)) return false;
    auto& lanes = song_.tracks[static_cast<std::size_t>(track)].automation;
    if (lane < 0 || lane >= static_cast<int>(lanes.size())) return false;
    auto& points = lanes[static_cast<std::size_t>(lane)].points;
    if (point < 0 || point >= static_cast<int>(points.size())) return false;
    checkpoint();
    points.erase(points.begin() + point);
    notifyStructureChanged();
    return true;
}

void SongModel::newTake() { take_recorded_ = false; }

void SongModel::checkpointTake() {
    if (take_recorded_) return;
    checkpoint();
    take_recorded_ = true;
}

void SongModel::setCapturing(bool capturing) { capturing_ = capturing; }

void SongModel::checkpointStrip(int track, const QString& merge) {
    if (capturing_ && captures(track)) {
        // Recorded into the take: the take's one step covers it.
        checkpointTake();
        return;
    }
    checkpoint(merge);
}
