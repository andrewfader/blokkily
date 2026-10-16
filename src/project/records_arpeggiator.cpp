// The track arpeggiator (features/arpeggiator.feature). Written only for a
// track whose arpeggiator is not at its defaults, so a project that never
// used one saves the records it did before; a file without the record loads
// every arpeggiator off.
//
//   arp <track> <on|off> <up|down|updown|played|random> <division> <octaves> <gate> <latch|free>
//
// Division is steps a beat (1, 2, 3, 4, 6 or 8), octaves 1 to 4, gate 0.05
// to 1. Anything else is refused rather than clamped.

#include "records.hpp"

#include <array>
#include <iomanip>

namespace blokkily::project_io {
namespace {

constexpr std::array<const char*, Arpeggiator::mode_count> mode_tokens{"up", "down", "updown",
                                                                       "played", "random"};

void write_arpeggiator(const Project& project, WriteContext& context) {
    for (std::size_t index = 0; index < project.song.tracks.size(); ++index) {
        const auto& arp = project.song.tracks[index].arp;
        if (arp == Arpeggiator{}) continue;
        context.out << "arp " << index << ' ' << (arp.enabled ? "on" : "off") << ' '
                    << mode_tokens[static_cast<std::size_t>(arp.mode)] << ' '
                    << static_cast<int>(arp.division) << ' ' << static_cast<int>(arp.octaves)
                    << ' ' << std::setprecision(4) << arp.gate << ' '
                    << (arp.latch ? "latch" : "free") << '\n';
    }
}

bool parse_arpeggiator(const Fields& fields, ParseContext& context) {
    if (!fields.count(8)) return context.fail("malformed arp record");
    const auto track = fields.integer(1);
    const auto& on = fields.tokens[2];
    std::size_t mode = 0;
    while (mode < mode_tokens.size() && fields.tokens[3] != mode_tokens[mode]) ++mode;
    const auto division = fields.integer(4);
    const auto octaves = fields.integer(5);
    const auto gate = fields.real(6);
    const auto& latch = fields.tokens[7];
    if (!track || *track < 0 || (on != "on" && on != "off") || mode == mode_tokens.size() ||
        !division || !octaves || !gate || (latch != "latch" && latch != "free"))
        return context.fail("malformed arp record");
    if (static_cast<std::size_t>(*track) >= context.tracks.size())
        return context.fail("arp refers to a track that does not exist");
    Arpeggiator arp;
    arp.enabled = on == "on";
    arp.mode = static_cast<Arpeggiator::Mode>(mode);
    arp.division = static_cast<std::uint8_t>(std::clamp<long long>(*division, 0, 255));
    arp.octaves = static_cast<std::uint8_t>(std::clamp<long long>(*octaves, 0, 255));
    arp.gate = static_cast<float>(*gate);
    arp.latch = latch == "latch";
    if (!arp.valid() || *division != arp.division || *octaves != arp.octaves)
        return context.fail("arp record out of range");
    context.tracks[static_cast<std::size_t>(*track)].arp = arp;
    return true;
}

constexpr std::array arpeggiator_handlers{
    RecordHandler{"arp", parse_arpeggiator},
};

} // namespace

const RecordModule& arpeggiator_records() {
    static const RecordModule module{"arpeggiator", write_arpeggiator, arpeggiator_handlers, nullptr};
    return module;
}

} // namespace blokkily::project_io
