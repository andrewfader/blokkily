#include "blokkily/project/project.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace blokkily {

namespace {

constexpr char empty_marker = '~';
constexpr std::string_view base64_alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

bool is_plain(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
           c == '.' || c == '_' || c == '-' || c == '/';
}

// Percent-escapes to a single whitespace-free token so that every record stays
// one parseable line regardless of what a plugin path or parameter id contains.
std::string escape(std::string_view text) {
    if (text.empty()) return std::string(1, empty_marker);
    std::string out;
    out.reserve(text.size());
    for (const unsigned char c : text) {
        if (is_plain(static_cast<char>(c))) {
            out.push_back(static_cast<char>(c));
        } else {
            std::array<char, 4> buffer{};
            std::snprintf(buffer.data(), buffer.size(), "%%%02X", c);
            out.append(buffer.data(), 3);
        }
    }
    return out;
}

std::optional<std::string> unescape(std::string_view token) {
    if (token.size() == 1 && token.front() == empty_marker) return std::string{};
    std::string out;
    out.reserve(token.size());
    for (std::size_t index = 0; index < token.size(); ++index) {
        if (token[index] != '%') { out.push_back(token[index]); continue; }
        if (index + 2 >= token.size()) return std::nullopt;
        const auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int high = digit(token[index + 1]);
        const int low = digit(token[index + 2]);
        if (high < 0 || low < 0) return std::nullopt;
        out.push_back(static_cast<char>(high * 16 + low));
        index += 2;
    }
    return out;
}

std::string encode_base64(std::span<const std::byte> data) {
    if (data.empty()) return std::string(1, empty_marker);
    std::string out;
    out.reserve((data.size() + 2) / 3 * 4);
    for (std::size_t index = 0; index < data.size(); index += 3) {
        const std::size_t remaining = data.size() - index;
        const auto byte = [&](std::size_t offset) {
            return offset < remaining ? static_cast<unsigned>(data[index + offset]) : 0U;
        };
        const unsigned triple = (byte(0) << 16) | (byte(1) << 8) | byte(2);
        out.push_back(base64_alphabet[(triple >> 18) & 0x3F]);
        out.push_back(base64_alphabet[(triple >> 12) & 0x3F]);
        out.push_back(remaining > 1 ? base64_alphabet[(triple >> 6) & 0x3F] : '=');
        out.push_back(remaining > 2 ? base64_alphabet[triple & 0x3F] : '=');
    }
    return out;
}

std::optional<std::vector<std::byte>> decode_base64(std::string_view text) {
    if (text.size() == 1 && text.front() == empty_marker) return std::vector<std::byte>{};
    if (text.empty() || text.size() % 4 != 0) return std::nullopt;

    std::size_t padding = 0;
    while (padding < 2 && text[text.size() - 1 - padding] == '=') ++padding;
    const std::string_view body = text.substr(0, text.size() - padding);
    if (body.find('=') != std::string_view::npos) return std::nullopt;

    std::vector<std::byte> out;
    out.reserve(text.size() / 4 * 3);
    for (std::size_t index = 0; index < text.size(); index += 4) {
        unsigned triple = 0;
        const std::size_t available = std::min<std::size_t>(4, body.size() - index);
        for (std::size_t offset = 0; offset < available; ++offset) {
            const auto position = base64_alphabet.find(body[index + offset]);
            if (position == std::string_view::npos) return std::nullopt;
            triple |= static_cast<unsigned>(position) << (18 - 6 * offset);
        }
        // A group of n encoded characters carries n - 1 bytes.
        for (std::size_t byte = 0; byte + 1 < available; ++byte)
            out.push_back(static_cast<std::byte>((triple >> (16 - 8 * byte)) & 0xFF));
    }
    return out;
}

// %.17g and strtod round-trip an IEEE double exactly, so tempo and parameter
// lock values survive a save/load without drifting.
std::string number(double value) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.17g", value);
    return buffer.data();
}

std::string number(float value) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.9g", static_cast<double>(value));
    return buffer.data();
}

struct Fields {
    std::vector<std::string> tokens;

    [[nodiscard]] bool count(std::size_t expected) const { return tokens.size() == expected; }
    [[nodiscard]] bool at_least(std::size_t expected) const { return tokens.size() >= expected; }

    [[nodiscard]] std::optional<long long> integer(std::size_t index) const {
        if (index >= tokens.size()) return std::nullopt;
        char* end = nullptr;
        const long long value = std::strtoll(tokens[index].c_str(), &end, 10);
        if (end == tokens[index].c_str() || *end != '\0') return std::nullopt;
        return value;
    }
    [[nodiscard]] std::optional<double> real(std::size_t index) const {
        if (index >= tokens.size()) return std::nullopt;
        char* end = nullptr;
        const double value = std::strtod(tokens[index].c_str(), &end);
        if (end == tokens[index].c_str() || *end != '\0') return std::nullopt;
        return value;
    }
    [[nodiscard]] std::optional<std::string> text(std::size_t index) const {
        if (index >= tokens.size()) return std::nullopt;
        return unescape(tokens[index]);
    }
};

Fields split(const std::string& line) {
    Fields fields;
    std::istringstream stream(line);
    std::string token;
    while (stream >> token) fields.tokens.push_back(token);
    return fields;
}

bool fail(std::string* error, std::string message) {
    if (error) *error = std::move(message);
    return false;
}

} // namespace

std::string ProjectFile::serialize(const Project& project) {
    std::ostringstream out;
    out << "blokkily-project " << format_version << '\n';
    out << "name " << escape(project.name) << '\n';
    out << "tempo " << number(project.tempo) << '\n';
    out << "master " << number(project.song.master_gain_db) << '\n';
    // The tuning travels in full rather than by name, so a session written in a
    // scale this build has never heard of still reloads as itself.
    out << "tuning " << escape(project.song.tuning.name) << ' '
        << number(project.song.tuning.period_cents) << ' '
        << project.song.tuning.anchor_key << ' '
        << project.song.tuning.degrees.size();
    for (const double cents : project.song.tuning.degrees) out << ' ' << number(cents);
    out << '\n';
    out << "scale " << escape(project.song.scale.name) << ' '
        << project.song.scale.cents.size();
    for (const double cents : project.song.scale.cents) out << ' ' << number(cents);
    out << '\n';
    out << "harmony " << project.song.root_degree << ' '
        << (project.song.auto_scale ? 1 : 0) << '\n';
    for (std::size_t index = 0; index < project.song.patterns.size(); ++index) {
        const auto& slot = project.song.patterns[index];
        out << "pattern " << escape(slot.name) << ' ' << slot.pattern.length() << ' '
            << slot.pattern.ticks_per_beat() << '\n';
        for (const auto& trigger : slot.pattern.events()) {
            out << "trigger " << index << ' ' << trigger.id << ' ' << trigger.start << ' '
                << trigger.duration << ' ' << trigger.micro_offset << ' '
                << number(trigger.probability) << ' '
                << static_cast<unsigned>(trigger.ratchets) << ' '
                << static_cast<unsigned>(trigger.play_on_loop);
            if (const auto* note = std::get_if<Note>(&trigger.musical_data)) {
                out << " note " << note->key << ' ' << number(note->velocity) << ' '
                    << number(note->release_velocity) << ' ' << number(note->cents);
            } else {
                const auto& chord = std::get<Chord>(trigger.musical_data);
                out << " chord " << chord.root << ' ' << static_cast<int>(chord.inversion) << ' '
                    << chord.strum << ' ' << chord.intervals.size();
                for (const auto interval : chord.intervals) out << ' ' << interval;
                // One retune per interval, always written, so a chord reads
                // back in the tuning it was played in.
                for (std::size_t voice = 0; voice < chord.intervals.size(); ++voice)
                    out << ' ' << number(voice < chord.cents.size() ? chord.cents[voice] : 0.0);
            }
            out << '\n';
            for (const auto& lock : trigger.locks)
                out << "lock " << index << ' ' << trigger.id << ' ' << escape(lock.parameter_id)
                    << ' ' << lock.parameter_index << ' '
                    << (lock.kind == ParameterLock::Kind::modulation ? "modulation" : "automation")
                    << ' ' << number(lock.value) << '\n';
        }
    }
    for (const auto& track : project.song.tracks)
        out << "track " << escape(track.name) << ' ' << number(track.mix.gain_db) << ' '
            << number(track.mix.pan) << ' ' << (track.mix.mute ? 1 : 0) << ' '
            << (track.mix.solo ? 1 : 0) << ' ' << escape(track.instrument.format) << ' '
            << escape(track.instrument.path) << ' ' << escape(track.instrument.identifier)
            << ' ' << encode_base64(track.instrument.state) << '\n';
    for (const auto& clip : project.song.clips)
        out << "clip " << clip.track << ' ' << clip.pattern << ' ' << clip.start << ' '
            << clip.repeats << '\n';
    return out.str();
}

namespace {

// Patterns are rebuilt only after the whole file is read, so a trigger record
// can be checked against the pattern it names no matter where it appears.
struct PatternDraft {
    std::string name;
    Tick length = 1920;
    Tick ticks_per_beat = 480;
    std::vector<Trigger> triggers;
};

} // namespace

std::optional<Project> ProjectFile::parse(const std::string& text, std::string* error) {
    std::istringstream stream(text);
    std::string line;
    if (!std::getline(stream, line)) {
        (void)fail(error, "project file is empty");
        return std::nullopt;
    }
    const auto header = split(line);
    if (!header.count(2) || header.tokens[0] != "blokkily-project") {
        (void)fail(error, "not a Blokkily project file");
        return std::nullopt;
    }
    const auto version = header.integer(1);
    if (!version || *version != format_version) {
        (void)fail(error, "unsupported project format version");
        return std::nullopt;
    }

    Project project;
    std::vector<PatternDraft> drafts;
    std::vector<Track> tracks;
    std::vector<Clip> clips;

    const auto find_trigger = [](PatternDraft& draft, EventId id) -> Trigger* {
        const auto it = std::find_if(draft.triggers.begin(), draft.triggers.end(),
            [id](const Trigger& trigger) { return trigger.id == id; });
        return it == draft.triggers.end() ? nullptr : &*it;
    };

    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        const auto fields = split(line);
        if (fields.tokens.empty()) continue;
        const std::string& record = fields.tokens.front();

        if (record == "name") {
            const auto value = fields.text(1);
            if (!fields.count(2) || !value) { (void)fail(error, "malformed name record"); return std::nullopt; }
            project.name = *value;
        } else if (record == "tempo") {
            const auto value = fields.real(1);
            if (!fields.count(2) || !value || *value <= 0.0) {
                (void)fail(error, "malformed tempo record"); return std::nullopt;
            }
            project.tempo = *value;
        } else if (record == "master") {
            const auto value = fields.real(1);
            if (!fields.count(2) || !value) {
                (void)fail(error, "malformed master record"); return std::nullopt;
            }
            project.song.master_gain_db = *value;
        } else if (record == "tuning") {
            const auto name = fields.text(1);
            const auto period = fields.real(2);
            const auto anchor = fields.integer(3);
            const auto count = fields.integer(4);
            if (!name || !period || !anchor || !count || *period <= 0.0 || *count <= 0 ||
                !fields.count(static_cast<std::size_t>(5 + *count))) {
                (void)fail(error, "malformed tuning record"); return std::nullopt;
            }
            Tuning tuning;
            tuning.name = *name;
            tuning.period_cents = *period;
            tuning.anchor_key = static_cast<int>(*anchor);
            tuning.degrees.clear();
            for (int degree = 0; degree < *count; ++degree) {
                const auto cents = fields.real(static_cast<std::size_t>(5 + degree));
                if (!cents) { (void)fail(error, "malformed tuning record"); return std::nullopt; }
                tuning.degrees.push_back(*cents);
            }
            project.song.tuning = std::move(tuning);
        } else if (record == "scale") {
            const auto name = fields.text(1);
            const auto count = fields.integer(2);
            if (!name || !count || *count < 0 ||
                !fields.count(static_cast<std::size_t>(3 + *count))) {
                (void)fail(error, "malformed scale record"); return std::nullopt;
            }
            Scale scale;
            scale.name = *name;
            for (int degree = 0; degree < *count; ++degree) {
                const auto cents = fields.real(static_cast<std::size_t>(3 + degree));
                if (!cents) { (void)fail(error, "malformed scale record"); return std::nullopt; }
                scale.cents.push_back(*cents);
            }
            project.song.scale = std::move(scale);
        } else if (record == "harmony") {
            const auto root = fields.integer(1);
            const auto automatic = fields.integer(2);
            if (!fields.count(3) || !root || !automatic) {
                (void)fail(error, "malformed harmony record"); return std::nullopt;
            }
            project.song.root_degree = static_cast<int>(*root);
            project.song.auto_scale = *automatic != 0;
        } else if (record == "pattern") {
            const auto name = fields.text(1);
            const auto length = fields.integer(2);
            const auto ticks = fields.integer(3);
            if (!fields.count(4) || !name || !length || !ticks || *length <= 0 || *ticks <= 0) {
                (void)fail(error, "malformed pattern record"); return std::nullopt;
            }
            drafts.push_back({*name, *length, *ticks, {}});
        } else if (record == "trigger") {
            if (!fields.at_least(10)) { (void)fail(error, "malformed trigger record"); return std::nullopt; }
            const auto pattern_index = fields.integer(1);
            const auto id = fields.integer(2);
            const auto start = fields.integer(3);
            const auto duration = fields.integer(4);
            const auto micro = fields.integer(5);
            const auto probability = fields.real(6);
            const auto ratchets = fields.integer(7);
            const auto loop = fields.integer(8);
            if (!pattern_index || !id || !start || !duration || !micro || !probability ||
                !ratchets || !loop || *id <= 0 || *pattern_index < 0 ||
                static_cast<std::size_t>(*pattern_index) >= drafts.size() ||
                *ratchets < 0 || *ratchets > 255 || *loop < 0 || *loop > 255) {
                (void)fail(error, "malformed trigger record"); return std::nullopt;
            }
            Trigger trigger;
            trigger.id = static_cast<EventId>(*id);
            trigger.start = *start;
            trigger.duration = *duration;
            trigger.micro_offset = *micro;
            trigger.probability = static_cast<float>(*probability);
            trigger.ratchets = static_cast<std::uint8_t>(*ratchets);
            trigger.play_on_loop = static_cast<std::uint8_t>(*loop);

            if (fields.tokens[9] == "note") {
                const auto key = fields.integer(10);
                const auto velocity = fields.real(11);
                const auto release = fields.real(12);
                const auto cents = fields.real(13);
                if (!fields.count(14) || !key || !velocity || !release || !cents) {
                    (void)fail(error, "malformed note payload"); return std::nullopt;
                }
                trigger.musical_data = Note{static_cast<std::int16_t>(*key),
                                            static_cast<float>(*velocity),
                                            static_cast<float>(*release), *cents};
            } else if (fields.tokens[9] == "chord") {
                const auto root = fields.integer(10);
                const auto inversion = fields.integer(11);
                const auto strum = fields.integer(12);
                const auto count = fields.integer(13);
                if (!root || !inversion || !strum || !count || *count < 0 ||
                    !fields.count(14 + 2 * static_cast<std::size_t>(*count))) {
                    (void)fail(error, "malformed chord payload"); return std::nullopt;
                }
                Chord chord;
                chord.root = static_cast<std::int16_t>(*root);
                chord.inversion = static_cast<std::int8_t>(*inversion);
                chord.strum = *strum;
                chord.intervals.clear();
                for (long long index = 0; index < *count; ++index) {
                    const auto interval = fields.integer(14 + static_cast<std::size_t>(index));
                    if (!interval) { (void)fail(error, "malformed chord interval"); return std::nullopt; }
                    chord.intervals.push_back(static_cast<std::int16_t>(*interval));
                }
                chord.cents.clear();
                for (long long index = 0; index < *count; ++index) {
                    const auto retune =
                        fields.real(14 + static_cast<std::size_t>(*count + index));
                    if (!retune) { (void)fail(error, "malformed chord retune"); return std::nullopt; }
                    chord.cents.push_back(*retune);
                }
                trigger.musical_data = chord;
            } else {
                (void)fail(error, "unknown musical payload"); return std::nullopt;
            }
            auto& draft = drafts[static_cast<std::size_t>(*pattern_index)];
            if (find_trigger(draft, trigger.id) != nullptr) {
                (void)fail(error, "duplicate trigger identifier"); return std::nullopt;
            }
            draft.triggers.push_back(std::move(trigger));
        } else if (record == "lock") {
            const auto pattern_index = fields.integer(1);
            const auto id = fields.integer(2);
            const auto parameter = fields.text(3);
            const auto index = fields.integer(4);
            const auto value = fields.real(6);
            if (!fields.count(7) || !pattern_index || !id || !parameter || !index || !value ||
                *pattern_index < 0 ||
                static_cast<std::size_t>(*pattern_index) >= drafts.size() ||
                *index < 0 || *index > 0x7FFFFFFF) {
                (void)fail(error, "malformed lock record"); return std::nullopt;
            }
            const std::string& kind_token = fields.tokens[5];
            if (kind_token != "automation" && kind_token != "modulation") {
                (void)fail(error, "unknown lock kind"); return std::nullopt;
            }
            auto* trigger = find_trigger(drafts[static_cast<std::size_t>(*pattern_index)],
                                         static_cast<EventId>(*id));
            if (trigger == nullptr) {
                (void)fail(error, "lock refers to an unknown trigger"); return std::nullopt;
            }
            trigger->locks.push_back({*parameter, static_cast<std::int32_t>(*index), *value,
                                      kind_token == "modulation"
                                          ? ParameterLock::Kind::modulation
                                          : ParameterLock::Kind::automation});
        } else if (record == "track") {
            const auto name = fields.text(1);
            const auto gain = fields.real(2);
            const auto pan = fields.real(3);
            const auto mute = fields.integer(4);
            const auto solo = fields.integer(5);
            const auto format = fields.text(6);
            const auto path = fields.text(7);
            const auto identifier = fields.text(8);
            if (!fields.count(10) || !name || !gain || !pan || !mute || !solo || !format ||
                !path || !identifier || *mute < 0 || *mute > 1 || *solo < 0 || *solo > 1) {
                (void)fail(error, "malformed track record"); return std::nullopt;
            }
            auto state = decode_base64(fields.tokens[9]);
            if (!state) { (void)fail(error, "malformed instrument state"); return std::nullopt; }
            Track track;
            track.name = *name;
            track.mix = {*gain, *pan, *mute == 1, *solo == 1};
            track.instrument = {*format, *path, *identifier, std::move(*state)};
            tracks.push_back(std::move(track));
        } else if (record == "clip") {
            const auto track = fields.integer(1);
            const auto pattern_index = fields.integer(2);
            const auto start = fields.integer(3);
            const auto repeats = fields.integer(4);
            if (!fields.count(5) || !track || !pattern_index || !start || !repeats ||
                *track < 0 || *pattern_index < 0 || *start < 0 || *repeats <= 0 ||
                *repeats > 0xFFFF) {
                (void)fail(error, "malformed clip record"); return std::nullopt;
            }
            clips.push_back({static_cast<std::size_t>(*track),
                             static_cast<std::size_t>(*pattern_index), *start,
                             static_cast<std::uint32_t>(*repeats)});
        } else {
            (void)fail(error, "unknown record: " + record);
            return std::nullopt;
        }
    }

    if (drafts.empty()) { (void)fail(error, "project has no pattern record"); return std::nullopt; }
    if (tracks.empty()) { (void)fail(error, "project has no track record"); return std::nullopt; }

    project.song.patterns.clear();
    for (auto& draft : drafts) {
        PatternSlot slot;
        slot.name = draft.name;
        try {
            slot.pattern = Pattern(draft.length, draft.ticks_per_beat);
            for (auto& trigger : draft.triggers)
                if (!slot.pattern.restore(std::move(trigger))) {
                    (void)fail(error, "trigger could not be restored"); return std::nullopt;
                }
        } catch (const std::invalid_argument& thrown) {
            (void)fail(error, thrown.what());
            return std::nullopt;
        }
        project.song.patterns.push_back(std::move(slot));
    }
    project.song.tracks = std::move(tracks);
    project.song.clips = std::move(clips);
    if (!project.song.consistent()) {
        (void)fail(error, "a clip refers to a track or pattern that does not exist");
        return std::nullopt;
    }
    return project;
}

bool ProjectFile::save(const Project& project, const std::filesystem::path& file,
                       std::string* error) {
    std::error_code failure;
    const auto parent = file.parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent, failure);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    if (!out) return fail(error, "cannot open project file for writing");
    out << serialize(project);
    if (!out) return fail(error, "failed while writing project file");
    return true;
}

std::optional<Project> ProjectFile::load(const std::filesystem::path& file, std::string* error) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        (void)fail(error, "cannot open project file for reading");
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parse(buffer.str(), error);
}

} // namespace blokkily
