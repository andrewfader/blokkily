// Chord velocity and voice length (plan item 1.6, conflict C2). The format 4
// `chord` line keeps its shape; how hard and how long each voice sounds travels
// in two additive records that follow it:
//
//   voicevel <pattern> <trigger-id> <chord-velocity> <n> v0 … v(n-1)
//   voicelen <pattern> <trigger-id> <n> d0 … d(n-1)
//
// `n` is zero (no per-voice velocities) or the chord's voice count. voicevel
// is written only when a chord differs from the default (0.8, nothing per
// voice) and voicelen only when a chord has per-voice lengths, so a file with
// neither — every format 4 file — plays its chords as it always did.

#include "records.hpp"

#include <array>

namespace blokkily::project_io {
namespace {

void write_voices(const Project& project, WriteContext& context) {
    auto& out = context.out;
    for (std::size_t index = 0; index < project.song.patterns.size(); ++index) {
        for (const auto& trigger : project.song.patterns[index].pattern.events()) {
            const auto* chord = std::get_if<Chord>(&trigger.musical_data);
            if (chord == nullptr) continue;
            if (chord->velocity != Chord{}.velocity || !chord->velocities.empty()) {
                out << "voicevel " << index << ' ' << trigger.id << ' '
                    << number(chord->velocity) << ' ' << chord->velocities.size();
                for (const float velocity : chord->velocities) out << ' ' << number(velocity);
                out << '\n';
            }
            if (!chord->durations.empty()) {
                out << "voicelen " << index << ' ' << trigger.id << ' '
                    << chord->durations.size();
                for (const Tick length : chord->durations) out << ' ' << length;
                out << '\n';
            }
        }
    }
}

// The chord a voice record names, or null after recording why.
Chord* chord_named(const Fields& fields, ParseContext& context, const char* record) {
    const auto pattern_index = fields.integer(1);
    const auto id = fields.integer(2);
    if (!pattern_index || !id || *pattern_index < 0 ||
        static_cast<std::size_t>(*pattern_index) >= context.drafts.size()) {
        (void)context.fail(std::string("malformed ") + record + " record");
        return nullptr;
    }
    auto* trigger = context.drafts[static_cast<std::size_t>(*pattern_index)].find(
        static_cast<EventId>(*id));
    if (trigger == nullptr) {
        (void)context.fail(std::string(record) + " refers to an unknown trigger");
        return nullptr;
    }
    auto* chord = std::get_if<Chord>(&trigger->musical_data);
    if (chord == nullptr) (void)context.fail(std::string(record) + " refers to a note, not a chord");
    return chord;
}

bool in_unit_range(double value) { return value >= 0.0 && value <= 1.0; }

bool parse_voicevel(const Fields& fields, ParseContext& context) {
    if (!fields.at_least(5)) return context.fail("malformed voicevel record");
    const auto velocity = fields.real(3);
    const auto count = fields.integer(4);
    if (!velocity || !count || *count < 0 || !in_unit_range(*velocity) ||
        !fields.count(5 + static_cast<std::size_t>(*count)))
        return context.fail("malformed voicevel record");
    auto* chord = chord_named(fields, context, "voicevel");
    if (chord == nullptr) return false;
    if (*count != 0 && static_cast<std::size_t>(*count) != chord->intervals.size())
        return context.fail("voicevel needs one velocity per voice of its chord");
    std::vector<float> velocities;
    for (long long voice = 0; voice < *count; ++voice) {
        const auto value = fields.real(5 + static_cast<std::size_t>(voice));
        if (!value || !in_unit_range(*value)) return context.fail("malformed voice velocity");
        velocities.push_back(static_cast<float>(*value));
    }
    chord->velocity = static_cast<float>(*velocity);
    chord->velocities = std::move(velocities);
    return true;
}

bool parse_voicelen(const Fields& fields, ParseContext& context) {
    if (!fields.at_least(4)) return context.fail("malformed voicelen record");
    const auto count = fields.integer(3);
    if (!count || *count <= 0 || !fields.count(4 + static_cast<std::size_t>(*count)))
        return context.fail("malformed voicelen record");
    auto* chord = chord_named(fields, context, "voicelen");
    if (chord == nullptr) return false;
    if (static_cast<std::size_t>(*count) != chord->intervals.size())
        return context.fail("voicelen needs one length per voice of its chord");
    std::vector<Tick> durations;
    for (long long voice = 0; voice < *count; ++voice) {
        const auto value = fields.integer(4 + static_cast<std::size_t>(voice));
        if (!value || *value <= 0) return context.fail("malformed voice length");
        durations.push_back(*value);
    }
    chord->durations = std::move(durations);
    return true;
}

constexpr std::array chord_velocity_handlers{
    RecordHandler{"voicevel", parse_voicevel},
    RecordHandler{"voicelen", parse_voicelen},
};

} // namespace

const RecordModule& chord_velocity_records() {
    static const RecordModule module{"chord_velocity", write_voices, chord_velocity_handlers,
                                     nullptr};
    return module;
}

} // namespace blokkily::project_io
