#pragma once

// Standard MIDI Files, the format every other sequencer, DAW and notation
// program reads and writes. A song leaves as what the engine plays (each
// track compiled by Song::arrange, the same compile the render callback and
// the bounce use) and arrives as sections of patterns, its notes written onto
// steps the way a recorded take is.

#include "blokkily/model/song.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace blokkily {

// One note of a file, in the file's ticks (ticks_per_quarter a quarter note).
struct MidiFileNote {
    Tick start = 0;
    Tick duration = 1;
    std::uint8_t channel = 0;
    std::uint8_t key = 60;
    std::uint8_t velocity = 100;   // 1..127

    friend bool operator==(const MidiFileNote&, const MidiFileNote&) = default;
};

// One controller movement of a file (pitch bend, control change, channel or
// poly pressure), its tick in the file's ticks.
struct MidiFileControl {
    std::uint8_t channel = 0;
    ContinuousEvent event;

    friend bool operator==(const MidiFileControl&, const MidiFileControl&) = default;
};

struct MidiFileTrack {
    std::string name;
    std::vector<MidiFileNote> notes;         // sorted by start, then key
    std::vector<MidiFileControl> controls;   // sorted by tick
};

struct MidiFileTempo {
    Tick at = 0;
    double bpm = 120.0;
};

struct MidiFileMeter {
    Tick at = 0;
    std::int16_t numerator = 4;
    std::int16_t denominator = 4;
};

struct MidiFile {
    std::uint16_t format = 1;
    std::uint16_t ticks_per_quarter = 480;
    std::vector<MidiFileTempo> tempos;   // sorted by tick
    std::vector<MidiFileMeter> meters;   // sorted by tick
    std::vector<MidiFileTrack> tracks;   // every MTrk chunk, in file order
    // The tick the last track ends on (its end-of-track event).
    Tick length = 0;
};

// The MIDI channel (0-based) track `track` of `song` is written on: drum
// tracks (named DRUM, PERC or KIT) on channel 10, the others in turn on the
// rest, wrapping after fifteen.
[[nodiscard]] std::uint8_t midi_channel_for(const Song& song, std::size_t track);

// The song as a format-1 file at the song's own resolution: a conductor track
// with the tempo map (a ramp as a tempo every sixteenth) and the meters, then
// one track per mixer track named after it, holding the notes and controller
// movements the engine plays on that track. Parameter automation, modulation,
// per-note expression, audio clips and retunes away from twelve-tone keys
// have no place in the format and are left out.
[[nodiscard]] std::vector<std::uint8_t> encode_midi_file(const Song& song);
bool write_midi_file(const std::filesystem::path& path, const Song& song,
                     std::string* error = nullptr);

// Reads format 0 and 1 files: running status, note-ons of velocity 0 as
// note-offs, a note re-struck before its release ended there, and a note
// never released ended with its track. SMPTE timing and format 2 are refused.
[[nodiscard]] std::optional<MidiFile> decode_midi_file(std::span<const std::uint8_t> bytes,
                                                       std::string* error = nullptr);
[[nodiscard]] std::optional<MidiFile> read_midi_file(const std::filesystem::path& path,
                                                     std::string* error = nullptr);

// What import_midi_file did.
struct MidiImport {
    std::size_t first_section = 0;
    std::size_t sections = 0;      // sections made
    std::size_t notes = 0;         // notes written
    std::size_t tracks_added = 0;  // mixer tracks the file needed beyond the song's
    Tick start = 0;                // where the first section was placed
};

// Brings `file` into `song` after everything the song already plays, starting
// on a bar line: the file is cut into sections of `bars_per_section` bars of
// the song's meter there, named "<name> 1", "<name> 2"..., each placed on
// every track. The file's tracks that hold notes or controllers go, in order,
// to the song's tracks (splitting MIDI channels), adding tracks (named after the file's, with no
// instrument) when the file has more. Each note lands on its nearest
// sixteenth with how far off the grid it was as micro-timing, notes on one
// step becoming a chord (write_played). Notes on the same step share timing;
// durations are capped to the section length, as with recorded takes.
// Times are kept in quarter notes; the
// song keeps its own tempo and meter. A stretch with nothing in it makes no
// section. Returns nothing, changing nothing, for a file with no notes.
[[nodiscard]] std::optional<MidiImport> import_midi_file(Song& song, const MidiFile& file,
                                                         const std::string& name,
                                                         int bars_per_section = 4);

} // namespace blokkily
