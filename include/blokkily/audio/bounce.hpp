#pragma once

#include "blokkily/audio/audio_writer.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace blokkily {

struct BounceReport {
    std::uint64_t frames = 0;
    float peak = 0.0F;
    bool clipped = false; // the float mix exceeded full scale before conversion
};

// Renders the arrangement to a file through the same SongEngine the speakers
// hear, so an export is the mix that was auditioned rather than a second
// rendering path that can drift from it. Runs as fast as the host allows and
// is deterministic: the same song and seed produce the same bytes, however
// long the engine played before, because every processor and delay line is
// reset first (SongEngine::reset_processing). Afterwards the transport is put
// back where it was, playing or not, and the processors are reset again so
// the export's tail does not ring on. The render callback must not be
// running (stop the device first): the bounce drives process() itself.
//
// `tail_frames` keeps the release of the last note instead of cutting the song
// off at its final tick; an insert effect's own tail (an echo, a reverb) is
// kept whole when it is longer. The engine's output latency (plugin delay
// compensation, decision 10) is trimmed from the start, so the file begins
// where the song does and runs song length plus tail.
[[nodiscard]] std::optional<BounceReport> bounce_song(
    SongEngine& engine, const std::filesystem::path& file, WaveFormat format,
    std::uint64_t tail_frames = 0, std::string* error = nullptr);

// What an export asks for beyond the song itself.
struct BounceOptions {
    // The metronome is left out of a bounce (decision 14) unless this asks
    // for it; then the click is rendered into the file exactly as it plays
    // live, at the level the session has. Either way the engine's click
    // setting is put back afterwards.
    bool include_metronome = false;
    // A stem is a post-insert, post-fader/pan bus tap, before downstream
    // returns/master processing. Empty selects the complete master mix.
    std::optional<OutputTap> source{};
    // Told after every block how many of the file's frames are written and
    // how many it will hold, so a window can show the export's progress.
    // Called on the bouncing thread between blocks, never inside one.
    std::function<void(std::uint64_t written, std::uint64_t total)> progress{};
    // The file's container. WAV is written at the bounce's WaveFormat; MP3
    // and Ogg Vorbis encode the same samples and ignore it.
    ExportContainer container = ExportContainer::wav;
    // TPDF dither when a WAV is written at 16 or 24 bits (WaveWriter::
    // set_dither). The same mix still writes the same bytes.
    bool dither = false;
};

[[nodiscard]] std::optional<BounceReport> bounce_song(
    SongEngine& engine, const std::filesystem::path& file, WaveFormat format,
    std::uint64_t tail_frames, const BounceOptions& options, std::string* error = nullptr);

// One file of a stem export: the bus it taps and where it was written.
struct StemFile {
    OutputTap source;
    std::string name;
    std::filesystem::path file;
    BounceReport report;
};

// Bounces every track, then every return, to a file of its own in
// `directory`, each a full bounce_song() of that bus's tap (post-insert,
// post-fader/pan, before master processing), so the files line up sample for
// sample with one another and with a bounce of the master. Files are named
// "NN Name.ext" after the bus, in mixer order. `options.source` is ignored and
// the click is never in a stem. `progress` is told about the whole export.
[[nodiscard]] std::optional<std::vector<StemFile>> bounce_stems(
    SongEngine& engine, const Song& song, const std::filesystem::path& directory,
    WaveFormat format, std::uint64_t tail_frames, const BounceOptions& options,
    std::string* error = nullptr);

// A file-name-safe form of a bus name: path separators and control
// characters become spaces, and an empty result is "Track".
[[nodiscard]] std::string stem_file_name(std::size_t index, const std::string& name);

} // namespace blokkily
