// The song's tempo and meter maps as the interface sees them (plan F-A): the
// bar layout every timeline view draws from, and the edits that change the
// timebase. The tempo lane and the meter menu (item 2.1) drive these.

#include "song_model.hpp"

#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>

blokkily::Tick SongModel::barStart(int bar) const {
    return song_.meter.bar_start(static_cast<std::int32_t>(bar));
}

blokkily::Tick SongModel::barTicks(int bar) const {
    return song_.meter.bar_length(static_cast<std::int32_t>(bar));
}

int SongModel::barAt(blokkily::Tick tick) const {
    return static_cast<int>(song_.meter.bar_at(tick));
}

QVariantList SongModel::barLayout() const {
    QVariantList layout;
    const int count = bars();
    const auto total = static_cast<double>(std::max<blokkily::Tick>(1, barStart(count)));
    for (int bar = 0; bar < count; ++bar) {
        const auto start = barStart(bar);
        const auto ticks = barTicks(bar);
        const auto& meter = song_.meter.meter_in(bar);
        QVariantMap entry;
        entry["start"] = static_cast<qint64>(start);
        entry["ticks"] = static_cast<qint64>(ticks);
        entry["numerator"] = static_cast<int>(meter.numerator);
        entry["denominator"] = static_cast<int>(meter.denominator);
        entry["x0"] = static_cast<double>(start) / total;
        entry["x1"] = static_cast<double>(start + ticks) / total;
        layout.push_back(entry);
    }
    return layout;
}

QVariantList SongModel::tempoPoints() const {
    QVariantList points;
    for (const auto& point : song_.tempo.points) {
        QVariantMap entry;
        entry["at"] = static_cast<qint64>(point.at);
        entry["bpm"] = point.bpm;
        entry["ramp"] = point.ramp;
        entry["bar"] = barAt(point.at);
        points.push_back(entry);
    }
    return points;
}

bool SongModel::setPatternSteps(int steps) {
    if (steps < 1 || steps > 64) return false;
    auto& pattern = editPattern();
    const auto length = static_cast<blokkily::Tick>(steps) * 120;
    if (pattern.length() == length) return true;
    // Clicks on the length spinner arrive one by one; each is its own step.
    checkpoint();
    pattern = pattern.with_length(length);
    song_.fit_clips(static_cast<std::size_t>(std::max(0, current_pattern_)));
    // The clips that play this pattern are now a different length, so the
    // arrangement is recompiled.
    notifyStructureChanged();
    return true;
}

namespace {
bool tick_in_range(double tick) {
    return std::isfinite(tick) && tick >= 0.0 &&
           tick < static_cast<double>(std::numeric_limits<std::int32_t>::max());
}
} // namespace

bool SongModel::setTempoAt(double bpm, double atTick) {
    if (!std::isfinite(bpm) || !tick_in_range(atTick)) return false;
    const double clamped = std::clamp(bpm, blokkily::minimum_bpm, blokkily::maximum_bpm);
    const auto at = static_cast<blokkily::Tick>(atTick);
    auto& points = song_.tempo.points;
    // The point in effect at `at`: the last one at or before it.
    auto found = std::upper_bound(points.begin(), points.end(), at,
        [](blokkily::Tick tick, const blokkily::TempoPoint& point) { return tick < point.at; });
    if (found == points.begin()) return false;
    --found;
    if (found->bpm == clamped) return true;
    // A readout dragged or scrolled sends a stream of these; they are one
    // step of history.
    checkpoint(QStringLiteral("tempo"));
    found->bpm = clamped;
    emit timebaseChanged();
    notifyStructureChanged();
    return true;
}

bool SongModel::setTempoPoint(double atTick, double bpm, bool ramp) {
    if (!std::isfinite(bpm) || !tick_in_range(atTick)) return false;
    if (bpm < blokkily::minimum_bpm || bpm > blokkily::maximum_bpm) return false;
    blokkily::TempoPoint point{static_cast<blokkily::Tick>(atTick), bpm, ramp};
    auto tempo = song_.tempo;
    tempo.set(point);
    if (tempo == song_.tempo) return true;
    checkpoint();
    song_.tempo = std::move(tempo);
    emit timebaseChanged();
    notifyStructureChanged();
    return true;
}

double SongModel::bpmAt(double atTick) const {
    if (!tick_in_range(atTick)) return song_.tempo.bpm_at(0);
    return song_.tempo.bpm_at(static_cast<blokkily::Tick>(atTick));
}

bool SongModel::removeTempoPoint(double atTick) {
    if (!tick_in_range(atTick)) return false;
    auto tempo = song_.tempo;
    if (!tempo.remove(static_cast<blokkily::Tick>(atTick))) return false;
    checkpoint();
    song_.tempo = std::move(tempo);
    emit timebaseChanged();
    notifyStructureChanged();
    return true;
}

bool SongModel::setMeter(int bar, int numerator, int denominator) {
    if (bar < 0) return false;
    blokkily::MeterMap meter = song_.meter;
    meter.set({static_cast<std::int32_t>(bar), static_cast<std::int16_t>(numerator),
               static_cast<std::int16_t>(denominator)});
    if (numerator < 1 || numerator > 64 || !meter.valid()) return false;
    if (meter == song_.meter) return true;
    checkpoint();
    const auto before = song_.meter;
    song_.meter = std::move(meter);
    // Clips and tempo points keep their bar numbers (decision 9).
    blokkily::rebar(song_, before);
    emit timebaseChanged();
    notifyStructureChanged();
    return true;
}
