// MIDI learn bindings (features/midi_learn.feature). Written only when the
// song has some, so a project without them saves the records it did before.
//
//   midi_learn <channel 1-16> <cc 0-119> <gain|pan|mute|solo|master|macro> <index>
//
// The index is a track for gain, pan, mute and solo, a modulator (a macro)
// for macro, and 0 for master. One binding per channel and controller.
// Anything else is refused rather than clamped.

#include "records.hpp"

#include <array>

namespace blokkily::project_io {
namespace {

constexpr std::array<const char*, MidiBinding::target_count> target_tokens{
    "gain", "pan", "mute", "solo", "master", "macro"};

void write_midi_learn(const Project& project, WriteContext& context) {
    for (const auto& binding : project.song.midi_bindings)
        context.out << "midi_learn " << binding.channel + 1 << ' ' << static_cast<int>(binding.controller)
                    << ' ' << target_tokens[static_cast<std::size_t>(binding.target)] << ' '
                    << binding.index << '\n';
}

bool parse_midi_learn(const Fields& fields, ParseContext& context) {
    if (!fields.count(5)) return context.fail("malformed midi_learn record");
    const auto channel = fields.integer(1);
    const auto controller = fields.integer(2);
    std::size_t target = 0;
    while (target < target_tokens.size() && fields.tokens[3] != target_tokens[target]) ++target;
    const auto index = fields.integer(4);
    if (!channel || !controller || !index || *channel < 1 || *channel > 16 || *controller < 0 ||
        *controller > 119 || target == target_tokens.size() || *index < 0 || *index > 0xFFFF)
        return context.fail("malformed midi_learn record");
    MidiBinding binding;
    binding.channel = static_cast<std::uint8_t>(*channel - 1);
    binding.controller = static_cast<std::uint8_t>(*controller);
    binding.target = static_cast<MidiBinding::Target>(target);
    binding.index = static_cast<std::uint32_t>(*index);
    for (const auto& existing : context.project.song.midi_bindings)
        if (existing.channel == binding.channel && existing.controller == binding.controller)
            return context.fail("more than one midi_learn record for a controller");
    context.project.song.midi_bindings.push_back(binding);
    return true;
}

// Once the tracks and modulators are in: every binding names one that exists.
bool finish_midi_learn(ParseContext& context) {
    const auto& song = context.project.song;
    for (const auto& binding : song.midi_bindings) {
        using Target = MidiBinding::Target;
        const bool valid =
            binding.target == Target::master_gain ? binding.index == 0
            : binding.target == Target::macro
                ? binding.index < song.modulators.size() &&
                      song.modulators[binding.index].kind == Modulator::Kind::macro
                : binding.index < song.tracks.size();
        if (!valid) return context.fail("midi_learn names a control the song does not have");
    }
    return true;
}

constexpr std::array midi_learn_handlers{
    RecordHandler{"midi_learn", parse_midi_learn},
};

} // namespace

const RecordModule& midi_learn_records() {
    static const RecordModule module{"midi_learn", write_midi_learn, midi_learn_handlers,
                                     finish_midi_learn};
    return module;
}

} // namespace blokkily::project_io
