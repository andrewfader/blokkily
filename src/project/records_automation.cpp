// Automation lanes and each track's automation mode (plan §F-E, item 1.5;
// decision 3).
//
//   automode <track> <off|read|touch|latch|write>
//   automation <track> <parameter|gain|pan|mute> <track|return|master> <bus> <slot>
//              <parameter-index> <parameter-id> <n> [<tick> <value>]*n
//
// A lane belongs to the track its record names and targets the processor its
// address names (slot -1 is the track's instrument, or the strip for gain, pan
// and mute). `automode` is written only for a mode other than read. Whether every
// target exists is checked by Song::consistent once the whole file is read.

#include "records.hpp"
#include "records_bus.hpp"

#include <array>
#include <limits>
#include <optional>

namespace blokkily::project_io {

namespace {

constexpr std::array mode_tokens{"off", "read", "touch", "latch", "write"};
constexpr std::array kind_tokens{"parameter", "gain", "pan", "mute"};

template <typename Enum, std::size_t N>
std::optional<Enum> token_to_enum(const std::array<const char*, N>& tokens,
                                  std::string_view token) {
    for (std::size_t i = 0; i < N; ++i)
        if (token == tokens[i]) return static_cast<Enum>(i);
    return std::nullopt;
}

void write_automation(const Project& project, WriteContext& context) {
    auto& out = context.out;
    const auto& tracks = project.song.tracks;
    for (std::size_t t = 0; t < tracks.size(); ++t)
        if (tracks[t].automation_mode != AutomationMode::read)
            out << "automode " << t << ' '
                << mode_tokens.at(static_cast<std::size_t>(tracks[t].automation_mode)) << '\n';
    for (std::size_t t = 0; t < tracks.size(); ++t)
        for (const auto& lane : tracks[t].automation) {
            const auto& target = lane.target;
            out << "automation " << t << ' '
                << kind_tokens.at(static_cast<std::size_t>(target.kind)) << ' '
                << bus_kind_token(target.processor.kind) << ' ' << target.processor.bus << ' '
                << target.processor.slot << ' ' << target.parameter_index << ' '
                << escape(target.parameter_id) << ' ' << lane.points.size();
            for (const auto& point : lane.points)
                out << ' ' << point.at << ' ' << number(point.value);
            out << '\n';
        }
}

bool parse_automode(const Fields& fields, ParseContext& context) {
    if (!fields.count(3)) return context.fail("malformed automode record");
    const auto track = fields.integer(1);
    const auto mode = token_to_enum<AutomationMode>(mode_tokens, fields.tokens[2]);
    if (!track || !mode || *track < 0) return context.fail("malformed automode record");
    if (static_cast<std::size_t>(*track) >= context.tracks.size())
        return context.fail("automode refers to a track that does not exist");
    context.tracks[static_cast<std::size_t>(*track)].automation_mode = *mode;
    return true;
}

bool parse_automation(const Fields& fields, ParseContext& context) {
    if (!fields.at_least(9)) return context.fail("malformed automation record");
    const auto track = fields.integer(1);
    const auto kind = token_to_enum<AutomationTarget::Kind>(kind_tokens, fields.tokens[2]);
    const auto bus_kind = parse_bus_kind(fields.tokens[3]);
    const auto bus = fields.integer(4);
    const auto slot = fields.integer(5);
    const auto index = fields.integer(6);
    const auto id = fields.text(7);
    const auto count = fields.integer(8);
    if (!track || !kind || !bus_kind || !bus || !slot || !index || !id || !count ||
        *track < 0 || *bus < 0 || *bus > std::numeric_limits<std::uint32_t>::max() ||
        *slot < -1 || *slot > std::numeric_limits<std::int32_t>::max() || *index < 0 ||
        *index > std::numeric_limits<std::int32_t>::max() || *count < 0 ||
        !fields.count(9 + 2 * static_cast<std::size_t>(*count)))
        return context.fail("malformed automation record");
    if (static_cast<std::size_t>(*track) >= context.tracks.size())
        return context.fail("automation refers to a track that does not exist");

    AutomationLane lane;
    lane.target.kind = *kind;
    lane.target.processor = {*bus_kind, static_cast<std::uint32_t>(*bus),
                             static_cast<std::int32_t>(*slot)};
    lane.target.parameter_index = static_cast<std::int32_t>(*index);
    lane.target.parameter_id = *id;
    for (long long i = 0; i < *count; ++i) {
        const auto at = fields.integer(9 + 2 * static_cast<std::size_t>(i));
        const auto value = fields.real(10 + 2 * static_cast<std::size_t>(i));
        if (!at || !value || *at < 0) return context.fail("malformed automation point");
        lane.points.push_back({*at, *value});
    }
    if (!lane.well_formed()) return context.fail("automation points are out of order");
    context.tracks[static_cast<std::size_t>(*track)].automation.push_back(std::move(lane));
    return true;
}

constexpr std::array automation_handlers{
    RecordHandler{"automode", parse_automode},
    RecordHandler{"automation", parse_automation},
};

} // namespace

const RecordModule& automation_records() {
    static const RecordModule module{"automation", write_automation, automation_handlers,
                                     nullptr};
    return module;
}

} // namespace blokkily::project_io
