// Track inputs and the recording offset (plan §F-E, item 1.5).
//
//   input <track> <armed> <none|midi|audio|midi+audio> <midi-channel>
//         <audio-first-channel> <audio-channels> <off|auto|on>
//   record-offset <samples>
//
// Written only when they differ from the defaults, so a project that never
// touched recording saves exactly the records format 4 did. Arm and input
// state are saved with the project (decision 5).

#include "records.hpp"

#include <array>
#include <optional>

namespace blokkily::project_io {

namespace {

constexpr std::array source_tokens{"none", "midi", "audio", "midi+audio"};
constexpr std::array monitor_tokens{"off", "auto", "on"};

template <typename Enum, std::size_t N>
std::optional<Enum> token_to_enum(const std::array<const char*, N>& tokens,
                                  std::string_view token) {
    for (std::size_t i = 0; i < N; ++i)
        if (token == tokens[i]) return static_cast<Enum>(i);
    return std::nullopt;
}

void write_input(const Project& project, WriteContext& context) {
    auto& out = context.out;
    const auto& song = project.song;
    for (std::size_t t = 0; t < song.tracks.size(); ++t) {
        const auto& input = song.tracks[t].input;
        if (input == TrackInput{}) continue;
        out << "input " << t << ' ' << (input.armed ? 1 : 0) << ' '
            << source_tokens.at(static_cast<std::size_t>(input.source)) << ' '
            << static_cast<int>(input.midi_channel) << ' ' << input.audio_first_channel << ' '
            << static_cast<unsigned>(input.audio_channels) << ' '
            << monitor_tokens.at(static_cast<std::size_t>(input.monitor)) << '\n';
    }
    if (song.record_offset_samples != 0)
        out << "record-offset " << song.record_offset_samples << '\n';
}

bool parse_input(const Fields& fields, ParseContext& context) {
    if (!fields.count(8)) return context.fail("malformed input record");
    const auto track = fields.integer(1);
    const auto armed = fields.integer(2);
    const auto source = token_to_enum<TrackInput::Source>(source_tokens, fields.tokens[3]);
    const auto channel = fields.integer(4);
    const auto first = fields.integer(5);
    const auto channels = fields.integer(6);
    const auto monitor = token_to_enum<TrackInput::Monitor>(monitor_tokens, fields.tokens[7]);
    if (!track || !armed || !source || !channel || !first || !channels || !monitor ||
        *track < 0 || *armed < 0 || *armed > 1 || *channel < -1 || *channel > 15 ||
        *first < 0 || *first > 0xFFFF || (*channels != 1 && *channels != 2))
        return context.fail("malformed input record");
    if (static_cast<std::size_t>(*track) >= context.tracks.size())
        return context.fail("input refers to a track that does not exist");
    auto& input = context.tracks[static_cast<std::size_t>(*track)].input;
    input.armed = *armed == 1;
    input.source = *source;
    input.midi_channel = static_cast<std::int8_t>(*channel);
    input.audio_first_channel = static_cast<std::uint16_t>(*first);
    input.audio_channels = static_cast<std::uint8_t>(*channels);
    input.monitor = *monitor;
    return true;
}

bool parse_record_offset(const Fields& fields, ParseContext& context) {
    const auto samples = fields.integer(1);
    if (!fields.count(2) || !samples) return context.fail("malformed record-offset record");
    context.project.song.record_offset_samples = *samples;
    return true;
}

constexpr std::array input_handlers{
    RecordHandler{"input", parse_input},
    RecordHandler{"record-offset", parse_record_offset},
};

} // namespace

const RecordModule& input_records() {
    static const RecordModule module{"input", write_input, input_handlers, nullptr};
    return module;
}

} // namespace blokkily::project_io
