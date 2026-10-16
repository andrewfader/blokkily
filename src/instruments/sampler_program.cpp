#include "blokkily/instruments/sampler_program.hpp"
#include "blokkily/model/number_text.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>
#include <string_view>
#include <system_error>

namespace blokkily {

namespace {

constexpr std::string_view magic = "blokkily-sampler";

void append_double(std::string& out, double value) {
    char buffer[64];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    out.append(buffer, result.ptr);
}

template <typename Integer>
void append_integer(std::string& out, Integer value) {
    char buffer[32];
    const auto result = std::to_chars(buffer, buffer + sizeof buffer, value);
    out.append(buffer, result.ptr);
}

void append_quoted(std::string& out, std::string_view text) {
    out += '"';
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else {
            out += c;
        }
    }
    out += '"';
}

std::string_view loop_name(LoopMode mode) {
    switch (mode) {
    case LoopMode::forward: return "forward";
    case LoopMode::ping_pong: return "ping_pong";
    case LoopMode::off: break;
    }
    return "off";
}

std::optional<LoopMode> loop_from(std::string_view name) {
    if (name == "off") return LoopMode::off;
    if (name == "forward") return LoopMode::forward;
    if (name == "ping_pong") return LoopMode::ping_pong;
    return std::nullopt;
}

// Splits one line into tokens; a token starting with a quote runs to the
// matching unescaped quote and is unescaped. False on an unterminated quote.
bool tokenize(std::string_view line, std::vector<std::string>& tokens) {
    tokens.clear();
    std::size_t at = 0;
    while (at < line.size()) {
        if (line[at] == ' ') {
            ++at;
            continue;
        }
        std::string token;
        if (line[at] == '"') {
            ++at;
            bool closed = false;
            while (at < line.size()) {
                const char c = line[at++];
                if (c == '"') {
                    closed = true;
                    break;
                }
                if (c == '\\') {
                    if (at >= line.size()) return false;
                    const char escaped = line[at++];
                    token += escaped == 'n' ? '\n' : escaped == 'r' ? '\r' : escaped;
                } else {
                    token += c;
                }
            }
            if (!closed) return false;
        } else {
            while (at < line.size() && line[at] != ' ') token += line[at++];
        }
        tokens.push_back(std::move(token));
    }
    return true;
}

template <typename Number>
bool read_number(const std::string& text, Number& value) {
    return read_number_text(text, value);
}

bool read_flag(const std::string& text, bool& value) {
    if (text == "0") value = false;
    else if (text == "1") value = true;
    else return false;
    return true;
}

bool fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

bool valid_zone(const SamplerZone& zone, std::string* error) {
    const auto key_ok = [](int key) { return key >= 0 && key <= 127; };
    const auto velocity_ok = [](int velocity) { return velocity >= 1 && velocity <= 127; };
    if (!key_ok(zone.root_key) || !key_ok(zone.low_key) || !key_ok(zone.high_key) ||
        zone.low_key > zone.high_key)
        return fail(error, "sampler zone keys out of range");
    if (!velocity_ok(zone.low_velocity) || !velocity_ok(zone.high_velocity) ||
        zone.low_velocity > zone.high_velocity)
        return fail(error, "sampler zone velocities out of range");
    if (zone.end != 0 && zone.end <= zone.start)
        return fail(error, "sampler zone ends before it starts");
    if (zone.loop != LoopMode::off && zone.loop_end <= zone.loop_start)
        return fail(error, "sampler zone loop ends before it starts");
    const auto& env = zone.envelope;
    const auto time_ok = [](double seconds) { return std::isfinite(seconds) && seconds >= 0.0; };
    if (!time_ok(env.attack_s) || !time_ok(env.decay_s) || !time_ok(env.release_s) ||
        !(env.sustain >= 0.0 && env.sustain <= 1.0))
        return fail(error, "sampler zone envelope out of range");
    if (!std::isfinite(zone.fine_cents) || !std::isfinite(zone.gain_db) ||
        !(zone.pan >= -1.0 && zone.pan <= 1.0))
        return fail(error, "sampler zone tuning, gain or pan out of range");
    if (zone.choke_group < 0) return fail(error, "sampler zone choke group is negative");
    return true;
}

} // namespace

std::vector<std::byte> serialize_sampler(const SamplerProgram& program) {
    std::string text(magic);
    text += ' ';
    append_integer(text, sampler_state_version);
    text += "\nmode ";
    text += program.mode == SamplerProgram::Mode::kit ? "kit" : "keyed";
    text += "\npolyphony ";
    append_integer(text, program.polyphony);
    text += '\n';
    for (const auto& zone : program.zones) {
        text += "zone ";
        append_quoted(text, zone.sample);
        const auto integer = [&text](auto value) {
            text += ' ';
            append_integer(text, value);
        };
        const auto real = [&text](double value) {
            text += ' ';
            append_double(text, value);
        };
        integer(zone.root_key);
        real(zone.fine_cents);
        integer(zone.low_key);
        integer(zone.high_key);
        integer(zone.low_velocity);
        integer(zone.high_velocity);
        integer(zone.start);
        integer(zone.end);
        text += ' ';
        text += loop_name(zone.loop);
        integer(zone.loop_start);
        integer(zone.loop_end);
        real(zone.envelope.attack_s);
        real(zone.envelope.decay_s);
        real(zone.envelope.sustain);
        real(zone.envelope.release_s);
        real(zone.gain_db);
        real(zone.pan);
        integer(zone.track_pitch ? 1 : 0);
        integer(zone.one_shot ? 1 : 0);
        integer(zone.choke_group);
        text += '\n';
    }
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    return bytes;
}

std::optional<SamplerProgram> parse_sampler(std::span<const std::byte> state, std::string* error) {
    const std::string_view text(reinterpret_cast<const char*>(state.data()), state.size());
    SamplerProgram program;
    std::vector<std::string> tokens;
    bool seen_header = false;
    bool seen_mode = false;
    bool seen_polyphony = false;
    std::size_t line_start = 0;
    int line_number = 0;
    while (line_start < text.size()) {
        auto line_end = text.find('\n', line_start);
        if (line_end == std::string_view::npos) line_end = text.size();
        const auto line = text.substr(line_start, line_end - line_start);
        line_start = line_end + 1;
        ++line_number;
        const auto where = "sampler state line " + std::to_string(line_number) + ": ";
        if (line.empty()) continue;
        if (!tokenize(line, tokens) || tokens.empty()) {
            fail(error, where + "unreadable");
            return std::nullopt;
        }
        if (!seen_header) {
            int version = 0;
            if (tokens.size() != 2 || tokens[0] != magic || !read_number(tokens[1], version)) {
                fail(error, "not a sampler state");
                return std::nullopt;
            }
            if (version != sampler_state_version) {
                fail(error, "sampler state version " + tokens[1] + " is not supported");
                return std::nullopt;
            }
            seen_header = true;
            continue;
        }
        const auto& kind = tokens[0];
        if (kind == "mode" && tokens.size() == 2 && !seen_mode) {
            if (tokens[1] == "keyed") program.mode = SamplerProgram::Mode::keyed;
            else if (tokens[1] == "kit") program.mode = SamplerProgram::Mode::kit;
            else {
                fail(error, where + "unknown mode");
                return std::nullopt;
            }
            seen_mode = true;
        } else if (kind == "polyphony" && tokens.size() == 2 && !seen_polyphony) {
            if (!read_number(tokens[1], program.polyphony) || program.polyphony < 1 ||
                program.polyphony > sampler_max_voices) {
                fail(error, where + "polyphony out of range");
                return std::nullopt;
            }
            seen_polyphony = true;
        } else if (kind == "zone" && tokens.size() == 22) {
            SamplerZone zone;
            zone.sample = tokens[1];
            const auto loop = loop_from(tokens[10]);
            const bool ok =
                read_number(tokens[2], zone.root_key) && read_number(tokens[3], zone.fine_cents) &&
                read_number(tokens[4], zone.low_key) && read_number(tokens[5], zone.high_key) &&
                read_number(tokens[6], zone.low_velocity) &&
                read_number(tokens[7], zone.high_velocity) && read_number(tokens[8], zone.start) &&
                read_number(tokens[9], zone.end) && loop.has_value() &&
                read_number(tokens[11], zone.loop_start) &&
                read_number(tokens[12], zone.loop_end) &&
                read_number(tokens[13], zone.envelope.attack_s) &&
                read_number(tokens[14], zone.envelope.decay_s) &&
                read_number(tokens[15], zone.envelope.sustain) &&
                read_number(tokens[16], zone.envelope.release_s) &&
                read_number(tokens[17], zone.gain_db) && read_number(tokens[18], zone.pan) &&
                read_flag(tokens[19], zone.track_pitch) && read_flag(tokens[20], zone.one_shot) &&
                read_number(tokens[21], zone.choke_group);
            if (!ok) {
                fail(error, where + "malformed zone");
                return std::nullopt;
            }
            zone.loop = *loop;
            std::string reason;
            if (!valid_zone(zone, &reason)) {
                fail(error, where + reason);
                return std::nullopt;
            }
            program.zones.push_back(std::move(zone));
        } else {
            fail(error, where + "unknown or malformed record '" + kind + "'");
            return std::nullopt;
        }
    }
    if (!seen_header) {
        fail(error, "not a sampler state");
        return std::nullopt;
    }
    return program;
}

bool validate_sampler(const SamplerProgram& program, std::string* error) {
    if (program.polyphony < 1 || program.polyphony > sampler_max_voices)
        return fail(error, "sampler polyphony out of range");
    for (std::size_t index = 0; index < program.zones.size(); ++index) {
        std::string reason;
        if (!valid_zone(program.zones[index], &reason))
            return fail(error, "zone " + std::to_string(index) + ": " + reason);
    }
    return true;
}

SamplerProgram default_sampler(SamplerProgram::Mode mode) {
    SamplerProgram program;
    program.mode = mode;
    return program;
}

bool slice_evenly(SamplerProgram& program, const SamplerZone& source, std::uint64_t source_frames,
                  int count, int first_key) {
    const std::uint64_t end = source.end != 0 ? source.end : source_frames;
    if (count < 1 || count > 128 || first_key < 0 || first_key + count - 1 > 127 ||
        end <= source.start || end - source.start < static_cast<std::uint64_t>(count))
        return false;
    const std::uint64_t length = end - source.start;
    const auto boundary = [&](int index) {
        // round(index * length / count) in integers, exact for any length:
        // index * length / count = index * q + index * r / count.
        const auto n = static_cast<std::uint64_t>(count);
        const auto i = static_cast<std::uint64_t>(index);
        const std::uint64_t q = length / n;
        const std::uint64_t r = length % n;
        return source.start + i * q + (2 * i * r + n) / (2 * n);
    };
    std::vector<SamplerZone> slices;
    slices.reserve(static_cast<std::size_t>(count));
    for (int index = 0; index < count; ++index) {
        SamplerZone slice = source;
        slice.start = boundary(index);
        slice.end = boundary(index + 1);
        slice.low_key = slice.high_key = slice.root_key = first_key + index;
        slice.fine_cents = 0.0;
        slice.loop = LoopMode::off;
        slice.loop_start = slice.loop_end = 0;
        slice.track_pitch = false;
        slice.one_shot = true;
        slice.choke_group = 0;
        slices.push_back(std::move(slice));
    }
    program.zones = std::move(slices);
    program.mode = SamplerProgram::Mode::kit;
    return true;
}

std::vector<const SamplerZone*> zones_for(const SamplerProgram& program, int key, int velocity) {
    std::vector<const SamplerZone*> found;
    for (const auto& zone : program.zones)
        if (key >= zone.low_key && key <= zone.high_key && velocity >= zone.low_velocity &&
            velocity <= zone.high_velocity)
            found.push_back(&zone);
    return found;
}

// --- Multi-sample import ----------------------------------------------------

namespace {

bool is_letter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_digit(char c) { return c >= '0' && c <= '9'; }

std::string lowered(std::string_view text) {
    std::string out(text);
    for (auto& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

// The name without its extension.
std::string_view stem_of(std::string_view name) {
    const auto slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos) name.remove_prefix(slash + 1);
    const auto dot = name.find_last_of('.');
    return dot == std::string_view::npos ? name : name.substr(0, dot);
}

} // namespace

std::optional<int> key_from_name(std::string_view name) {
    const auto stem = stem_of(name);
    std::optional<int> found;
    for (std::size_t at = 0; at < stem.size(); ++at) {
        const char letter = stem[at];
        const char upper = (letter >= 'a' && letter <= 'g') ? static_cast<char>(letter - 'a' + 'A') : letter;
        if (upper < 'A' || upper > 'G') continue;
        // A note name stands alone: not inside a word.
        if (at > 0 && is_letter(stem[at - 1])) continue;
        std::size_t next = at + 1;
        int accidental = 0;
        if (next < stem.size() && (stem[next] == '#' || stem[next] == 's')) {
            // "s" only as "Cs4", never the start of a word such as "Cs" + letter.
            if (stem[next] == '#' || (next + 1 < stem.size() && (is_digit(stem[next + 1]) || stem[next + 1] == '-')))
                { accidental = 1; ++next; }
        } else if (next < stem.size() && stem[next] == 'b' && next + 1 < stem.size() &&
                   (is_digit(stem[next + 1]) || stem[next + 1] == '-')) {
            accidental = -1;
            ++next;
        }
        bool negative = false;
        if (next < stem.size() && stem[next] == '-' && next + 1 < stem.size() && is_digit(stem[next + 1])) {
            negative = true;
            ++next;
        }
        if (next >= stem.size() || !is_digit(stem[next])) continue;
        int octave = stem[next] - '0';
        ++next;
        // One digit of octave, and no digit straight after (that is a number).
        if (next < stem.size() && is_digit(stem[next])) continue;
        if (next < stem.size() && is_letter(stem[next]) && stem[next] != 'v') continue;
        if (negative) octave = -octave;
        static constexpr int steps[] = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
        const int key = (octave + 1) * 12 + steps[upper - 'A'] + accidental;
        if (key >= 0 && key <= 127) found = key;
    }
    return found;
}

std::optional<int> layer_from_name(std::string_view name) {
    const auto stem = lowered(stem_of(name));
    // Split into words at anything that is not a letter or a digit.
    std::vector<std::string> words;
    std::string word;
    for (const char c : stem) {
        if (is_letter(c) || is_digit(c)) word += c;
        else if (!word.empty()) { words.push_back(word); word.clear(); }
    }
    if (!word.empty()) words.push_back(word);
    std::optional<int> found;
    static const std::pair<const char*, int> dynamics[] = {
        {"ppp", 1}, {"pp", 2}, {"p", 3}, {"mp", 4}, {"mf", 5}, {"f", 6}, {"ff", 7}, {"fff", 8}};
    for (const auto& token : words) {
        for (const auto& [dynamic, rank] : dynamics)
            if (token == dynamic) found = rank;
        for (const char* prefix : {"vel", "v", "layer", "l"}) {
            const std::string_view lead(prefix);
            if (token.size() > lead.size() && token.compare(0, lead.size(), lead) == 0 &&
                std::all_of(token.begin() + static_cast<std::ptrdiff_t>(lead.size()), token.end(), is_digit)) {
                found = std::stoi(token.substr(lead.size()));
                break;
            }
        }
    }
    return found;
}

SamplerProgram multisample_program(std::vector<MultisampleSource> sources) {
    SamplerProgram program = default_sampler(SamplerProgram::Mode::keyed);
    if (sources.empty()) return program;
    std::vector<int> layers;
    for (const auto& source : sources) layers.push_back(source.layer);
    std::sort(layers.begin(), layers.end());
    layers.erase(std::unique(layers.begin(), layers.end()), layers.end());
    const int count = static_cast<int>(layers.size());
    for (int rank = 0; rank < count; ++rank) {
        std::vector<MultisampleSource> in_layer;
        for (const auto& source : sources)
            if (source.layer == layers[static_cast<std::size_t>(rank)] &&
                std::none_of(in_layer.begin(), in_layer.end(),
                             [&](const auto& kept) { return kept.root_key == source.root_key; }))
                in_layer.push_back(source);
        std::stable_sort(in_layer.begin(), in_layer.end(),
                         [](const auto& a, const auto& b) { return a.root_key < b.root_key; });
        const int low_velocity = 1 + rank * 127 / count;
        const int high_velocity = (rank + 1) * 127 / count;
        for (std::size_t i = 0; i < in_layer.size(); ++i) {
            auto zone = in_layer[i].zone;
            zone.root_key = std::clamp(in_layer[i].root_key, 0, 127);
            zone.low_key = i == 0 ? 0 : (in_layer[i - 1].root_key + zone.root_key) / 2 + 1;
            zone.high_key = i + 1 == in_layer.size() ? 127 : (zone.root_key + in_layer[i + 1].root_key) / 2;
            zone.low_velocity = low_velocity;
            zone.high_velocity = high_velocity;
            zone.track_pitch = true;
            zone.one_shot = false;
            program.zones.push_back(zone);
        }
    }
    return program;
}

} // namespace blokkily
