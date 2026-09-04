#pragma once

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace blokkily {

struct BounceReport {
    std::uint64_t frames = 0;
    float peak = 0.0F;
    bool clipped = false; // the float mix exceeded full scale before conversion
};

// Renders the arrangement to a file through the same SongEngine the speakers
// hear, so an export is the mix that was auditioned rather than a second
// rendering path that can drift from it. Runs as fast as the host allows and
// is deterministic: the same song and seed produce the same bytes.
//
// `tail_frames` keeps the release of the last note instead of cutting the song
// off at its final tick.
[[nodiscard]] std::optional<BounceReport> bounce_song(
    SongEngine& engine, const std::filesystem::path& file, WaveFormat format,
    std::uint64_t tail_frames = 0, std::string* error = nullptr);

} // namespace blokkily
