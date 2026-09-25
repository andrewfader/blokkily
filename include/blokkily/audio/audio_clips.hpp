#pragma once

// Audio clips outside the render callback (item 2.2): finding the decoded
// audio a song's clips play, and the musical arithmetic of a clip, whose
// length is in frames of its file while the song is laid out in ticks.
// Control thread only.

#include "blokkily/audio/audio_asset.hpp"
#include "blokkily/model/song.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace blokkily {

// What load_clip_assets found. Indexed like Song::audio_files.
struct ClipAssetReport {
    std::vector<bool> missing;
    std::vector<std::string> reasons; // empty where the file loaded
    [[nodiscard]] std::size_t missing_count() const;
};

// The decoded audio of every file the song names, at `engine_rate`, indexed
// like song.audio_files, through `cache` (so a file already decoded at that
// rate is not decoded again). A file that cannot be read, or that no longer
// has the frames, rate and channels the song recorded for it, is a null entry:
// missing, never played wrong.
[[nodiscard]] AudioAssets load_clip_assets(const Song& song, AudioAssetCache& cache,
                                           double engine_rate,
                                           ClipAssetReport* report = nullptr);

// The fractional song tick where a clip's last frame ends, through the song's
// tempo map: its frames are seconds, and seconds are not a fixed number of
// ticks. The same arithmetic Song::length() uses.
[[nodiscard]] double audio_clip_end_tick(const Song& song, const AudioClip& clip);

// How many frames of a file at `rate` sound between two ticks of the song.
// Negative when `to` is before `from`.
[[nodiscard]] double frames_between(const Song& song, double from, double to,
                                    std::uint32_t rate);

// Adds `file` to the song's file list, or finds it there if the same file with
// the same header is already listed, and returns its index.
std::size_t add_audio_file(Song& song, const AudioFileRef& file);

} // namespace blokkily
