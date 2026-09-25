// Return buses, insert effects and sends (plan §F-E, item 1.5).
//
//   return <name> <gain-db> <pan> <mute> <solo>
//   insert <track|return|master> <bus> <format> <path> <identifier> <state>
//          <bypass> <n> [<parameter-id> <value>]*n
//   send <track> <return> <level-db> <pre|post>
//
// Returns are numbered by the order of their records, and each bus's inserts
// run in the order of theirs. An insert or send names a bus whose record came
// earlier in the file; the writer always puts tracks and returns first.

#include "records.hpp"
#include "records_bus.hpp"

#include <array>
#include <limits>

namespace blokkily::project_io {

namespace {

void write_inserts(std::ostream& out, BusKind kind, std::size_t bus,
                   const std::vector<EffectSlot>& inserts) {
    for (const auto& slot : inserts) {
        out << "insert " << bus_kind_token(kind) << ' ' << bus << ' '
            << escape(slot.plugin.format) << ' ' << escape(slot.plugin.path) << ' '
            << escape(slot.plugin.identifier) << ' ' << encode_base64(slot.plugin.state) << ' '
            << (slot.bypass ? 1 : 0) << ' ' << slot.parameters.size();
        for (const auto& parameter : slot.parameters)
            out << ' ' << parameter.id << ' ' << number(parameter.value);
        out << '\n';
    }
}

void write_effects(const Project& project, WriteContext& context) {
    auto& out = context.out;
    const auto& song = project.song;
    for (const auto& bus : song.returns)
        out << "return " << escape(bus.name) << ' ' << number(bus.mix.gain_db) << ' '
            << number(bus.mix.pan) << ' ' << (bus.mix.mute ? 1 : 0) << ' '
            << (bus.mix.solo ? 1 : 0) << '\n';
    for (std::size_t t = 0; t < song.tracks.size(); ++t)
        write_inserts(out, BusKind::track, t, song.tracks[t].inserts);
    for (std::size_t r = 0; r < song.returns.size(); ++r)
        write_inserts(out, BusKind::ret, r, song.returns[r].inserts);
    write_inserts(out, BusKind::master, 0, song.master_inserts);
    for (std::size_t t = 0; t < song.tracks.size(); ++t)
        for (const auto& send : song.tracks[t].sends)
            out << "send " << t << ' ' << send.bus << ' ' << number(send.level_db) << ' '
                << (send.pre_fader ? "pre" : "post") << '\n';
}

bool parse_return(const Fields& fields, ParseContext& context) {
    const auto name = fields.text(1);
    const auto gain = fields.real(2);
    const auto pan = fields.real(3);
    const auto mute = fields.integer(4);
    const auto solo = fields.integer(5);
    if (!fields.count(6) || !name || !gain || !pan || !mute || !solo || *mute < 0 ||
        *mute > 1 || *solo < 0 || *solo > 1)
        return context.fail("malformed return record");
    ReturnBus bus;
    bus.name = *name;
    bus.mix = {*gain, *pan, *mute == 1, *solo == 1};
    context.project.song.returns.push_back(std::move(bus));
    return true;
}

bool parse_insert(const Fields& fields, ParseContext& context) {
    if (!fields.at_least(9)) return context.fail("malformed insert record");
    const auto kind = parse_bus_kind(fields.tokens[1]);
    const auto bus = fields.integer(2);
    const auto format = fields.text(3);
    const auto path = fields.text(4);
    const auto identifier = fields.text(5);
    auto state = decode_base64(fields.tokens[6]);
    const auto bypass = fields.integer(7);
    const auto count = fields.integer(8);
    if (!kind || !bus || !format || !path || !identifier || !state || !bypass || !count ||
        *bus < 0 || *bypass < 0 || *bypass > 1 || *count < 0 ||
        !fields.count(9 + 2 * static_cast<std::size_t>(*count)))
        return context.fail("malformed insert record");

    std::vector<EffectSlot>* chain = nullptr;
    const auto index = static_cast<std::size_t>(*bus);
    switch (*kind) {
    case BusKind::track:
        if (index < context.tracks.size()) chain = &context.tracks[index].inserts;
        break;
    case BusKind::ret:
        if (index < context.project.song.returns.size())
            chain = &context.project.song.returns[index].inserts;
        break;
    case BusKind::master:
        if (index == 0) chain = &context.project.song.master_inserts;
        break;
    }
    if (chain == nullptr) return context.fail("insert refers to a bus that does not exist");

    EffectSlot slot;
    slot.plugin = {*format, *path, *identifier, std::move(*state)};
    slot.bypass = *bypass == 1;
    for (long long i = 0; i < *count; ++i) {
        const auto id = fields.integer(9 + 2 * static_cast<std::size_t>(i));
        const auto value = fields.real(10 + 2 * static_cast<std::size_t>(i));
        if (!id || !value || *id < std::numeric_limits<std::int32_t>::min() ||
            *id > std::numeric_limits<std::int32_t>::max())
            return context.fail("malformed insert parameter");
        slot.parameters.push_back({static_cast<std::int32_t>(*id), *value});
    }
    chain->push_back(std::move(slot));
    return true;
}

bool parse_send(const Fields& fields, ParseContext& context) {
    const auto track = fields.integer(1);
    const auto bus = fields.integer(2);
    const auto level = fields.real(3);
    if (!fields.count(5) || !track || !bus || !level || *track < 0 || *bus < 0 ||
        (fields.tokens[4] != "pre" && fields.tokens[4] != "post"))
        return context.fail("malformed send record");
    if (static_cast<std::size_t>(*track) >= context.tracks.size())
        return context.fail("send refers to a track that does not exist");
    if (static_cast<std::size_t>(*bus) >= context.project.song.returns.size())
        return context.fail("send refers to a return that does not exist");
    context.tracks[static_cast<std::size_t>(*track)].sends.push_back(
        {static_cast<std::size_t>(*bus), *level, fields.tokens[4] == "pre"});
    return true;
}

constexpr std::array effects_handlers{
    RecordHandler{"return", parse_return},
    RecordHandler{"insert", parse_insert},
    RecordHandler{"send", parse_send},
};

} // namespace

const RecordModule& effects_records() {
    static const RecordModule module{"effects", write_effects, effects_handlers, nullptr};
    return module;
}

} // namespace blokkily::project_io
