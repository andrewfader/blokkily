#include "blokkily/project/midi_file.hpp"

#include "blokkily/sequencer/take.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <tuple>

namespace blokkily {
namespace {

// The editors' grid: a step is a sixteenth at 480 ticks a quarter.
constexpr Tick ticks_per_step = 120;

using Bytes = std::vector<std::uint8_t>;

void put_u16(Bytes& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void put_u32(Bytes& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
}

void put_varlen(Bytes& out, std::uint32_t value) {
    std::uint8_t groups[5];
    int count = 0;
    do {
        groups[count++] = static_cast<std::uint8_t>(value & 0x7FU);
        value >>= 7;
    } while (value != 0 && count < 5);
    while (count > 1) out.push_back(static_cast<std::uint8_t>(groups[--count] | 0x80U));
    out.push_back(groups[0]);
}

// One event of a track before its delta times are written. `order` breaks
// ties on one tick: meta first, then releases, then controllers, then notes,
// so a note re-struck on the tick its previous one ends is heard again.
struct Pending {
    Tick at = 0;
    int order = 0;
    Bytes bytes;   // status and data, or FF type length data for a meta event
};

void put_meta(std::vector<Pending>& events, Tick at, std::uint8_t type, const Bytes& data) {
    Pending event{at, 0, {0xFF, type}};
    put_varlen(event.bytes, static_cast<std::uint32_t>(data.size()));
    event.bytes.insert(event.bytes.end(), data.begin(), data.end());
    events.push_back(std::move(event));
}

void put_name(std::vector<Pending>& events, const std::string& name) {
    put_meta(events, 0, 0x03, Bytes(name.begin(), name.end()));
}

Bytes track_chunk(std::vector<Pending> events, Tick end) {
    std::stable_sort(events.begin(), events.end(), [](const Pending& left, const Pending& right) {
        return std::tie(left.at, left.order) < std::tie(right.at, right.order);
    });
    Bytes body;
    Tick now = 0;
    for (const auto& event : events) {
        const Tick at = std::max<Tick>(event.at, now);
        put_varlen(body, static_cast<std::uint32_t>(at - now));
        body.insert(body.end(), event.bytes.begin(), event.bytes.end());
        now = at;
    }
    put_varlen(body, static_cast<std::uint32_t>(std::max<Tick>(0, end - now)));
    body.insert(body.end(), {0xFF, 0x2F, 0x00});
    Bytes chunk{'M', 'T', 'r', 'k'};
    put_u32(chunk, static_cast<std::uint32_t>(body.size()));
    chunk.insert(chunk.end(), body.begin(), body.end());
    return chunk;
}

std::uint8_t velocity_byte(float velocity) {
    return static_cast<std::uint8_t>(std::clamp(std::lround(velocity * 127.0F), 1L, 127L));
}

void put_tempo(std::vector<Pending>& events, Tick at, double bpm) {
    const auto micros = static_cast<std::uint32_t>(std::lround(60'000'000.0 / bpm));
    put_meta(events, at, 0x51,
             {static_cast<std::uint8_t>(micros >> 16), static_cast<std::uint8_t>(micros >> 8),
              static_cast<std::uint8_t>(micros)});
}

bool drum_name(const std::string& name) {
    std::string upper;
    std::transform(name.begin(), name.end(), std::back_inserter(upper),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return upper.find("DRUM") != std::string::npos || upper.find("PERC") != std::string::npos ||
           upper.find("KIT") != std::string::npos;
}

// --- Reading ----------------------------------------------------------------

struct Reader {
    std::span<const std::uint8_t> bytes;
    std::size_t at = 0;
    bool failed = false;

    [[nodiscard]] bool has(std::size_t count) const {
        return !failed && at <= bytes.size() && count <= bytes.size() - at;
    }
    std::uint8_t u8() {
        if (!has(1)) {
            failed = true;
            return 0;
        }
        return bytes[at++];
    }
    std::uint32_t u16() {
        const auto high = u8();
        return (std::uint32_t{high} << 8) | u8();
    }
    std::uint32_t u32() {
        const auto high = u16();
        return (high << 16) | u16();
    }
    std::uint32_t varlen() {
        std::uint32_t value = 0;
        for (int count = 0; count < 4; ++count) {
            const auto byte = u8();
            value = (value << 7) | (byte & 0x7FU);
            if ((byte & 0x80U) == 0) return value;
        }
        failed = true;
        return 0;
    }
};

bool fail(std::string* error, const char* message) {
    if (error != nullptr) *error = message;
    return false;
}

bool read_track(Reader& chunk, MidiFile& file, MidiFileTrack& track, Tick& end,
                std::string* error) {
    Tick now = 0;
    std::uint8_t running = 0;
    // Held notes by channel and key: where each began and how hard.
    std::map<std::pair<int, int>, std::pair<Tick, std::uint8_t>> held;
    const auto release = [&](int channel, int key, Tick at) {
        const auto found = held.find({channel, key});
        if (found == held.end()) return;
        track.notes.push_back({found->second.first, std::max<Tick>(1, at - found->second.first),
                               static_cast<std::uint8_t>(channel),
                               static_cast<std::uint8_t>(key), found->second.second});
        held.erase(found);
    };
    while (chunk.has(1)) {
        now += static_cast<Tick>(chunk.varlen());
        std::uint8_t status = chunk.u8();
        if (chunk.failed) break;
        if (status == 0xFF) {
            running = 0;
            const auto type = chunk.u8();
            const auto length = chunk.varlen();
            if (!chunk.has(length)) return fail(error, "a meta event runs past its track");
            const auto data = chunk.bytes.subspan(chunk.at, length);
            chunk.at += length;
            if (type == 0x2F) break;
            if (type == 0x03 && track.name.empty())
                track.name.assign(data.begin(), data.end());
            if (type == 0x51 && length == 3) {
                const auto micros = (std::uint32_t{data[0]} << 16) | (std::uint32_t{data[1]} << 8) |
                                    data[2];
                if (micros > 0) file.tempos.push_back({now, 60'000'000.0 / micros});
            }
            if (type == 0x58 && length >= 2 && data[1] < 6)
                file.meters.push_back({now, static_cast<std::int16_t>(data[0]),
                                       static_cast<std::int16_t>(1 << data[1])});
            continue;
        }
        if (status == 0xF0 || status == 0xF7) {
            running = 0;
            const auto length = chunk.varlen();
            if (!chunk.has(length)) return fail(error, "a system exclusive runs past its track");
            chunk.at += length;
            continue;
        }
        if (status >= 0xF0) return fail(error, "invalid MIDI channel status");
        std::uint8_t first = 0;
        if ((status & 0x80U) == 0) {
            if (running == 0) return fail(error, "running status with no status before it");
            first = status;
            status = running;
        } else {
            running = status;
            first = chunk.u8();
        }
        const auto kind = status & 0xF0U;
        const int channel = status & 0x0F;
        const bool two = kind != 0xC0U && kind != 0xD0U;
        const std::uint8_t second = two ? chunk.u8() : 0;
        if (chunk.failed) break;
        if (first > 127 || second > 127) return fail(error, "invalid MIDI data byte");
        if (kind == 0x90U && second > 0) {
            release(channel, first, now);   // re-struck: the old one ends here
            held[{channel, first}] = {now, second};
        } else if (kind == 0x80U || kind == 0x90U) {
            release(channel, first, now);
        } else if (const auto movement = continuous_from_midi(
                       status | (std::uint32_t{first} << 8) | (std::uint32_t{second} << 16),
                       now)) {
            track.controls.push_back({static_cast<std::uint8_t>(channel), *movement});
        }
    }
    if (chunk.failed) return fail(error, "a track ends in the middle of an event");
    for (auto it = held.begin(); it != held.end();) {
        const auto [channel, key] = it->first;
        ++it;
        release(channel, key, now);
    }
    end = std::max(end, now);
    std::sort(track.notes.begin(), track.notes.end(), [](const auto& left, const auto& right) {
        return std::tie(left.start, left.key) < std::tie(right.start, right.key);
    });
    std::stable_sort(track.controls.begin(), track.controls.end(),
                     [](const auto& left, const auto& right) {
                         return left.event.tick < right.event.tick;
                     });
    return true;
}

} // namespace

std::uint8_t midi_channel_for(const Song& song, std::size_t track) {
    if (track < song.tracks.size() && drum_name(song.tracks[track].name)) return 9;
    std::size_t melodic = 0;
    for (std::size_t index = 0; index < track && index < song.tracks.size(); ++index)
        if (!drum_name(song.tracks[index].name)) ++melodic;
    const auto channel = static_cast<std::uint8_t>(melodic % 15);
    return channel >= 9 ? static_cast<std::uint8_t>(channel + 1) : channel;
}

std::vector<std::uint8_t> encode_midi_file(const Song& song) {
    const Tick end = song.length();
    Bytes out{'M', 'T', 'h', 'd'};
    put_u32(out, 6);
    put_u16(out, 1);
    put_u16(out, static_cast<std::uint16_t>(song.tracks.size() + 1));
    put_u16(out, static_cast<std::uint16_t>(song.ticks_per_beat()));

    // The conductor: the tempo map and the meters, at the song's resolution.
    const Tick beat = song.ticks_per_beat();
    std::vector<Pending> conductor;
    put_name(conductor, "Blokkily");
    const auto& points = song.tempo.points;
    for (std::size_t index = 0; index < points.size(); ++index) {
        const auto& point = points[index];
        put_tempo(conductor, point.at, point.bpm);
        if (!point.ramp || index + 1 >= points.size()) continue;
        // A ramp moves linearly in ticks; a file holds steps, so it is
        // written as a tempo every sixteenth, each the ramp's tempo there.
        const auto& next = points[index + 1];
        const Tick stride = std::max<Tick>(1, beat / 4);
        for (Tick at = point.at + stride; at < next.at; at += stride)
            put_tempo(conductor, at, song.tempo.bpm_at(at));
    }
    for (const auto& change : song.meter.changes) {
        int power = 0;
        while ((1 << power) < change.denominator && power < 6) ++power;
        put_meta(conductor, song.meter.bar_start(change.bar) * beat / 480, 0x58,
                 {static_cast<std::uint8_t>(change.numerator), static_cast<std::uint8_t>(power),
                  24, 8});
    }
    const auto conductor_chunk = track_chunk(std::move(conductor), end);
    out.insert(out.end(), conductor_chunk.begin(), conductor_chunk.end());

    for (std::size_t track = 0; track < song.tracks.size(); ++track) {
        const auto channel = midi_channel_for(song, track);
        std::vector<Pending> events;
        put_name(events, song.tracks[track].name);
        const auto played = song.arrange(track, 0);
        for (const auto& note : played.notes) {
            if (note.key < 0 || note.key > 127) continue;
            // A note played ahead of the first downbeat sounds from the
            // downbeat, as the engine plays it, rather than being left out.
            const Tick start = std::max<Tick>(0, note.start);
            const Tick end = note.start + note.duration;
            if (end <= start) continue;
            const auto key = static_cast<std::uint8_t>(note.key);
            events.push_back({start, 3,
                              {static_cast<std::uint8_t>(0x90U | channel), key,
                               velocity_byte(note.velocity)}});
            events.push_back({std::max<Tick>(start + 1, end), 1,
                              {static_cast<std::uint8_t>(0x80U | channel), key, 64}});
        }
        for (const auto& movement : played.continuous) {
            const auto& event = movement.event;
            Pending message{movement.start, 2, {}};
            switch (event.kind) {
            case ContinuousEvent::Kind::pitch_bend:
                message.bytes = {static_cast<std::uint8_t>(0xE0U | channel),
                                 static_cast<std::uint8_t>(event.value & 0x7FU),
                                 static_cast<std::uint8_t>((event.value >> 7) & 0x7FU)};
                break;
            case ContinuousEvent::Kind::control_change:
                message.bytes = {static_cast<std::uint8_t>(0xB0U | channel), event.controller,
                                 static_cast<std::uint8_t>(event.value & 0x7FU)};
                break;
            case ContinuousEvent::Kind::channel_pressure:
                message.bytes = {static_cast<std::uint8_t>(0xD0U | channel),
                                 static_cast<std::uint8_t>(event.value & 0x7FU)};
                break;
            case ContinuousEvent::Kind::poly_pressure:
                message.bytes = {static_cast<std::uint8_t>(0xA0U | channel), event.controller,
                                 static_cast<std::uint8_t>(event.value & 0x7FU)};
                break;
            }
            events.push_back(std::move(message));
        }
        const auto chunk = track_chunk(std::move(events), end);
        out.insert(out.end(), chunk.begin(), chunk.end());
    }
    return out;
}

bool write_midi_file(const std::filesystem::path& path, const Song& song, std::string* error) {
    const auto bytes = encode_midi_file(song);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return fail(error, "cannot open the MIDI file for writing");
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    file.close();
    if (!file) return fail(error, "cannot write the MIDI file");
    return true;
}

std::optional<MidiFile> decode_midi_file(std::span<const std::uint8_t> bytes, std::string* error) {
    Reader reader{bytes};
    if (!reader.has(14) || bytes[0] != 'M' || bytes[1] != 'T' || bytes[2] != 'h' ||
        bytes[3] != 'd') {
        (void)fail(error, "not a Standard MIDI File");
        return std::nullopt;
    }
    reader.at = 4;
    const auto header = reader.u32();
    MidiFile file;
    file.format = static_cast<std::uint16_t>(reader.u16());
    const auto count = reader.u16();
    const auto division = reader.u16();
    if (header < 6 || !reader.has(header - 6)) {
        (void)fail(error, "the file's header is cut short");
        return std::nullopt;
    }
    reader.at += header - 6;
    if (count == 0 || (file.format == 0 && count != 1)) {
        (void)fail(error, "invalid MIDI track count");
        return std::nullopt;
    }
    if (file.format > 1) {
        (void)fail(error, "format 2 files (independent patterns) are not read");
        return std::nullopt;
    }
    if ((division & 0x8000U) != 0 || division == 0) {
        (void)fail(error, "SMPTE-timed files are not read");
        return std::nullopt;
    }
    file.ticks_per_quarter = static_cast<std::uint16_t>(division);
    for (std::uint32_t index = 0; index < count && reader.has(8); ++index) {
        const bool is_track = bytes[reader.at] == 'M' && bytes[reader.at + 1] == 'T' &&
                              bytes[reader.at + 2] == 'r' && bytes[reader.at + 3] == 'k';
        reader.at += 4;
        const auto length = reader.u32();
        if (!reader.has(length)) {
            (void)fail(error, "a track runs past the end of the file");
            return std::nullopt;
        }
        if (!is_track) {   // an unknown chunk is skipped, as the standard asks
            reader.at += length;
            --index;
            continue;
        }
        Reader chunk{bytes.subspan(reader.at, length)};
        reader.at += length;
        MidiFileTrack track;
        if (!read_track(chunk, file, track, file.length, error)) return std::nullopt;
        file.tracks.push_back(std::move(track));
    }
    if (file.tracks.size() != count) {
        (void)fail(error, "missing MIDI track chunk");
        return std::nullopt;
    }
    const auto by_tick = [](const auto& left, const auto& right) { return left.at < right.at; };
    std::stable_sort(file.tempos.begin(), file.tempos.end(), by_tick);
    std::stable_sort(file.meters.begin(), file.meters.end(), by_tick);
    return file;
}

std::optional<MidiFile> read_midi_file(const std::filesystem::path& path, std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        (void)fail(error, "cannot open the MIDI file");
        return std::nullopt;
    }
    const Bytes bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return decode_midi_file(bytes, error);
}

std::optional<MidiImport> import_midi_file(Song& song, const MidiFile& file,
                                           const std::string& name, int bars_per_section) {
    const Tick beat = song.ticks_per_beat();
    const auto scale = [&](Tick ticks) {
        return static_cast<Tick>(std::llround(static_cast<double>(ticks) * beat /
                                              std::max<int>(1, file.ticks_per_quarter)));
    };
    // A format-0 file can carry all sixteen instruments in one chunk.
    // Keep each channel separate before projecting it onto instrument tracks.
    std::vector<MidiFileTrack> split;
    for (const auto& track : file.tracks) {
        std::map<std::uint8_t, MidiFileTrack> channels;
        for (const auto& note : track.notes) channels[note.channel].notes.push_back(note);
        for (const auto& control : track.controls) channels[control.channel].controls.push_back(control);
        for (auto& [channel, part] : channels) {
            part.name = track.name;
            if (channels.size() > 1)
                part.name += " CH " + std::to_string(channel + 1);
            split.push_back(std::move(part));
        }
    }
    std::vector<const MidiFileTrack*> sources;
    std::size_t total = 0;
    for (const auto& track : split) {
        sources.push_back(&track);
        total += track.notes.size();
    }
    if (total == 0) return std::nullopt;

    if (!song.sections_whole()) song.adopt_sections();
    MidiImport report;
    while (song.tracks.size() < sources.size()) {
        Track added;
        const auto& source = *sources[song.tracks.size()];
        added.name = source.name.empty() ? "MIDI " + std::to_string(song.tracks.size() + 1)
                                         : source.name;
        song.tracks.push_back(std::move(added));
        song.add_track_parts(song.tracks.size() - 1);
        ++report.tracks_added;
    }

    // After everything the song plays, on a bar line.
    const Tick played = song.clips.empty() && song.audio_clips.empty() ? 0 : song.length();
    std::int32_t bar = song.meter.bar_at(played);
    if (song.meter.bar_start(bar) < played) ++bar;
    report.start = song.meter.bar_start(bar);
    const Tick section_length =
        song.meter.bar_length(bar) * std::max(1, bars_per_section);
    const Tick section_steps = std::max<Tick>(1, section_length / ticks_per_step);

    // Sparse files need storage only for sections that actually contain events.
    std::map<std::size_t, std::size_t> made;
    const auto section_for = [&](std::size_t stretch) {
        if (!made.contains(stretch)) {
            made[stretch] = song.add_section(name + " " + std::to_string(stretch + 1),
                                             section_length, beat);
            ++report.sections;
        }
        return made.at(stretch);
    };
    for (std::size_t index = 0; index < sources.size(); ++index) {
        for (const auto& note : sources[index]->notes) {
            const Tick start = scale(note.start);
            const auto step = (start + ticks_per_step / 2) / ticks_per_step;
            const auto stretch = static_cast<std::size_t>(step / section_steps);
            const auto section = section_for(stretch);
            auto& pattern = song.patterns[*song.part(section, index)].pattern;
            PlayedNote played_note;
            played_note.start = start - static_cast<Tick>(stretch) * section_length;
            played_note.duration = std::max<Tick>(1, scale(note.duration));
            played_note.key = note.key;
            played_note.velocity = static_cast<float>(note.velocity) / 127.0F;
            // A file's repeated pitch inside one step is a roll, kept as a
            // ratchet rather than dropped.
            (void)write_played(pattern, played_note, ticks_per_step, true);
            ++report.notes;
        }
        for (const auto& control : sources[index]->controls) {
            const Tick at = scale(control.event.tick);
            const auto stretch = static_cast<std::size_t>(at / section_length);
            const auto section = section_for(stretch);
            auto movement = control.event;
            movement.tick = at - static_cast<Tick>(stretch) * section_length;
            write_continuous(song.patterns[*song.part(section, index)].pattern, movement);
        }
    }
    for (const auto& [stretch, section] : made)
        (void)song.place_section(section, report.start + static_cast<Tick>(stretch) * section_length);
    report.first_section = made.begin()->second;
    return report;
}

} // namespace blokkily
