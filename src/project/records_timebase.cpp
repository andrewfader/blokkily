// The song's timebase (plan F-A, item 1.2).
//
//   tempo_point <tick> <bpm> <step|ramp>
//   meter <bar> <numerator> <denominator>
//
// Every tempo point and every meter change is written, in order, so a file
// always says what tempo it plays at. A file with neither loads at 120 BPM in
// 4/4. The legacy `tempo <bpm>` line (read by the core module, never written)
// is one tempo point at tick 0; a file that has it and tempo_point records
// too has two tempo sources, and is refused (plan C3).

#include "records.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace blokkily::project_io {

namespace {

void write_timebase(const Project& project, WriteContext& context) {
    auto& out = context.out;
    for (const auto& point : project.song.tempo.points)
        out << "tempo_point " << point.at << ' ' << number(point.bpm) << ' '
            << (point.ramp ? "ramp" : "step") << '\n';
    for (const auto& change : project.song.meter.changes)
        out << "meter " << change.bar << ' ' << change.numerator << ' ' << change.denominator
            << '\n';
}

bool parse_tempo_point(const Fields& fields, ParseContext& context) {
    const auto at = fields.integer(1);
    const auto bpm = fields.real(2);
    if (!fields.count(4) || !at || !bpm || *at < 0 || !std::isfinite(*bpm))
        return context.fail("malformed tempo_point record");
    const std::string& shape = fields.tokens[3];
    if (shape != "step" && shape != "ramp") return context.fail("unknown tempo_point shape");
    if (*bpm < minimum_bpm || *bpm > maximum_bpm)
        return context.fail("tempo_point tempo is outside 20 to 300 BPM");
    for (const auto& point : context.tempo_points)
        if (point.at == *at) return context.fail("two tempo_point records share a tick");
    context.tempo_points.push_back({*at, *bpm, shape == "ramp"});
    return true;
}

bool parse_meter(const Fields& fields, ParseContext& context) {
    const auto bar = fields.integer(1);
    const auto numerator = fields.integer(2);
    const auto denominator = fields.integer(3);
    if (!fields.count(4) || !bar || !numerator || !denominator || *bar < 0 ||
        *bar > 0x7FFFFFFF || *numerator < 1 || *numerator > 64 || *denominator < 1 ||
        *denominator > 32)
        return context.fail("malformed meter record");
    for (const auto& change : context.meter_changes)
        if (change.bar == *bar) return context.fail("two meter records share a bar");
    context.meter_changes.push_back({static_cast<std::int32_t>(*bar),
                                     static_cast<std::int16_t>(*numerator),
                                     static_cast<std::int16_t>(*denominator)});
    return true;
}

bool finish_timebase(ParseContext& context) {
    auto& song = context.project.song;
    if (context.legacy_tempo && !context.tempo_points.empty())
        return context.fail("a tempo record cannot be mixed with tempo_point records");
    if (context.legacy_tempo) {
        if (*context.legacy_tempo < minimum_bpm || *context.legacy_tempo > maximum_bpm)
            return context.fail("tempo is outside 20 to 300 BPM");
        song.tempo.points = {TempoPoint{0, *context.legacy_tempo, false}};
    } else if (!context.tempo_points.empty()) {
        auto points = std::move(context.tempo_points);
        std::sort(points.begin(), points.end(),
                  [](const TempoPoint& a, const TempoPoint& b) { return a.at < b.at; });
        if (points.front().at != 0) return context.fail("the first tempo_point is not at tick 0");
        song.tempo.points = std::move(points);
    }
    if (!context.meter_changes.empty()) {
        auto changes = std::move(context.meter_changes);
        std::sort(changes.begin(), changes.end(),
                  [](const MeterChange& a, const MeterChange& b) { return a.bar < b.bar; });
        if (changes.front().bar != 0) return context.fail("the first meter is not at bar 0");
        song.meter.changes = std::move(changes);
        if (!song.meter.valid())
            return context.fail("meter denominator is not a power of two up to 32");
    }
    context.legacy_tempo.reset();
    context.tempo_points.clear();
    context.meter_changes.clear();
    return true;
}

constexpr std::array timebase_handlers{
    RecordHandler{"tempo_point", parse_tempo_point},
    RecordHandler{"meter", parse_meter},
};

} // namespace

const RecordModule& timebase_records() {
    static const RecordModule module{"timebase", write_timebase, timebase_handlers,
                                     finish_timebase};
    return module;
}

} // namespace blokkily::project_io
