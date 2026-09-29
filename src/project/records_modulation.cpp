// Sidechain keys, instrument outputs and modulators (phase 2, waves 5.1 and
// 5.2).
//
//   sidechain <track|return|master> <bus> <slot> <source-track>
//   auxsource <track> <source-track> <output>
//   modulator <lfo|macro|follower> <name> <shape> <rate-hz> <sync-beats> <value>
//             <source-track> <attack-ms> <release-ms>
//   modtarget <modulator> <track|return|master> <bus> <slot> <parameter-index> <depth>
//
// A sidechain record names an insert written earlier by the effects module
// and the track that keys it. An auxsource record makes a track the channel
// of output <output> (1 is the first after the main one) of the instrument on
// <source-track>. Modulators are numbered by the order of their
// records; a modtarget names one written before it. Every field of a
// modulator is written whatever its kind, so a file reads back exactly. The
// records are additive: a project saved before them has no key and no
// modulator, which is what it loads with. Ranges, loops and dangling targets
// are refused by Song::consistent once the whole file is read.

#include "records.hpp"
#include "records_bus.hpp"

#include <array>
#include <limits>
#include <optional>

namespace blokkily::project_io {

namespace {

constexpr std::array kind_tokens{"lfo", "macro", "follower"};
constexpr std::array shape_tokens{"sine", "triangle", "saw-up", "saw-down", "square", "random"};

template <typename Enum, std::size_t N>
std::optional<Enum> token_to_enum(const std::array<const char*, N>& tokens,
                                  std::string_view token) {
    for (std::size_t i = 0; i < N; ++i)
        if (token == tokens[i]) return static_cast<Enum>(i);
    return std::nullopt;
}

void write_keys(std::ostream& out, BusKind kind, std::size_t bus,
                const std::vector<EffectSlot>& inserts) {
    for (std::size_t slot = 0; slot < inserts.size(); ++slot)
        if (inserts[slot].sidechain)
            out << "sidechain " << bus_kind_token(kind) << ' ' << bus << ' ' << slot << ' '
                << *inserts[slot].sidechain << '\n';
}

void write_modulation(const Project& project, WriteContext& context) {
    auto& out = context.out;
    const auto& song = project.song;
    for (std::size_t t = 0; t < song.tracks.size(); ++t)
        write_keys(out, BusKind::track, t, song.tracks[t].inserts);
    for (std::size_t r = 0; r < song.returns.size(); ++r)
        write_keys(out, BusKind::ret, r, song.returns[r].inserts);
    write_keys(out, BusKind::master, 0, song.master_inserts);
    for (std::size_t t = 0; t < song.tracks.size(); ++t)
        if (const auto& source = song.tracks[t].source)
            out << "auxsource " << t << ' ' << source->track << ' ' << source->output << '\n';
    for (std::size_t m = 0; m < song.modulators.size(); ++m) {
        const auto& modulator = song.modulators[m];
        out << "modulator " << kind_tokens.at(static_cast<std::size_t>(modulator.kind)) << ' '
            << escape(modulator.name) << ' '
            << shape_tokens.at(static_cast<std::size_t>(modulator.shape)) << ' '
            << number(modulator.rate_hz) << ' ' << number(modulator.sync_beats) << ' '
            << number(modulator.value) << ' ' << modulator.source_track << ' '
            << number(modulator.attack_ms) << ' ' << number(modulator.release_ms) << '\n';
        for (const auto& target : modulator.targets)
            out << "modtarget " << m << ' ' << bus_kind_token(target.processor.kind) << ' '
                << target.processor.bus << ' ' << target.processor.slot << ' '
                << target.parameter_index << ' ' << number(target.depth) << '\n';
    }
}

bool parse_sidechain(const Fields& fields, ParseContext& context) {
    const auto kind = fields.count(5) ? parse_bus_kind(fields.tokens[1]) : std::nullopt;
    const auto bus = fields.integer(2);
    const auto slot = fields.integer(3);
    const auto source = fields.integer(4);
    if (!kind || !bus || !slot || !source || *bus < 0 || *slot < 0 || *source < 0 ||
        *source > std::numeric_limits<std::uint32_t>::max())
        return context.fail("malformed sidechain record");
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
    if (chain == nullptr || static_cast<std::size_t>(*slot) >= chain->size())
        return context.fail("sidechain refers to an insert that does not exist");
    (*chain)[static_cast<std::size_t>(*slot)].sidechain = static_cast<std::uint32_t>(*source);
    return true;
}

bool parse_auxsource(const Fields& fields, ParseContext& context) {
    const auto track = fields.count(4) ? fields.integer(1) : std::nullopt;
    const auto source = fields.integer(2);
    const auto output = fields.integer(3);
    if (!track || !source || !output || *track < 0 || *source < 0 || *output < 0 ||
        *source > std::numeric_limits<std::uint32_t>::max() ||
        *output > std::numeric_limits<std::uint32_t>::max())
        return context.fail("malformed auxsource record");
    if (static_cast<std::size_t>(*track) >= context.tracks.size())
        return context.fail("auxsource refers to a track that does not exist");
    auto& fed = context.tracks[static_cast<std::size_t>(*track)];
    if (fed.source) return context.fail("a track has two auxsource records");
    fed.source = InstrumentOutput{static_cast<std::uint32_t>(*source),
                                  static_cast<std::uint32_t>(*output)};
    return true;
}

bool parse_modulator(const Fields& fields, ParseContext& context) {
    if (!fields.count(10)) return context.fail("malformed modulator record");
    const auto kind = token_to_enum<Modulator::Kind>(kind_tokens, fields.tokens[1]);
    const auto name = fields.text(2);
    const auto shape = token_to_enum<LfoShape>(shape_tokens, fields.tokens[3]);
    const auto rate = fields.real(4);
    const auto sync = fields.real(5);
    const auto value = fields.real(6);
    const auto source = fields.integer(7);
    const auto attack = fields.real(8);
    const auto release = fields.real(9);
    if (!kind || !name || !shape || !rate || !sync || !value || !source || !attack ||
        !release || *source < 0 || *source > std::numeric_limits<std::uint32_t>::max())
        return context.fail("malformed modulator record");
    Modulator modulator;
    modulator.kind = *kind;
    modulator.name = *name;
    modulator.shape = *shape;
    modulator.rate_hz = *rate;
    modulator.sync_beats = *sync;
    modulator.value = *value;
    modulator.source_track = static_cast<std::uint32_t>(*source);
    modulator.attack_ms = *attack;
    modulator.release_ms = *release;
    context.project.song.modulators.push_back(std::move(modulator));
    return true;
}

bool parse_modtarget(const Fields& fields, ParseContext& context) {
    const auto modulator = fields.integer(1);
    const auto kind = fields.count(7) ? parse_bus_kind(fields.tokens[2]) : std::nullopt;
    const auto bus = fields.integer(3);
    const auto slot = fields.integer(4);
    const auto parameter = fields.integer(5);
    const auto depth = fields.real(6);
    if (!modulator || !kind || !bus || !slot || !parameter || !depth || *modulator < 0 ||
        *bus < 0 || *bus > std::numeric_limits<std::uint32_t>::max() || *slot < -1 ||
        *slot > std::numeric_limits<std::int32_t>::max() || *parameter < 0 ||
        *parameter > std::numeric_limits<std::int32_t>::max())
        return context.fail("malformed modtarget record");
    auto& modulators = context.project.song.modulators;
    if (static_cast<std::size_t>(*modulator) >= modulators.size())
        return context.fail("modtarget refers to a modulator that does not exist");
    modulators[static_cast<std::size_t>(*modulator)].targets.push_back(
        {{*kind, static_cast<std::uint32_t>(*bus), static_cast<std::int32_t>(*slot)},
         static_cast<std::int32_t>(*parameter),
         *depth});
    return true;
}

constexpr std::array modulation_handlers{
    RecordHandler{"sidechain", parse_sidechain},
    RecordHandler{"auxsource", parse_auxsource},
    RecordHandler{"modulator", parse_modulator},
    RecordHandler{"modtarget", parse_modtarget},
};

} // namespace

const RecordModule& modulation_records() {
    static const RecordModule module{"modulation", write_modulation, modulation_handlers, nullptr};
    return module;
}

} // namespace blokkily::project_io
