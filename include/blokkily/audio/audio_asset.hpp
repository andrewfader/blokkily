#pragma once

#include "blokkily/audio/audio_file.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace blokkily {

// Frames summarised by one entry of AudioAsset::peaks.
inline constexpr std::size_t audio_peak_block = 256;

// Decoded audio ready to be played: at the rate it was loaded for, with a
// min/max overview for drawing. Immutable once shared. The audio thread only
// ever holds raw pointers into it; the owning shared_ptr is released on the
// control thread.
struct AudioAsset {
    std::uint32_t rate = 0;
    std::uint64_t frames = 0;
    std::vector<float> left;
    std::vector<float> right;   // empty => mono
    std::optional<LoopPoints> loop;
    std::optional<int> root_key;
    std::vector<std::pair<float, float>> peaks;   // min/max per audio_peak_block frames
};

using AudioAssetPtr = std::shared_ptr<const AudioAsset>;
// Indexed like Song::audio_files; a null entry is a missing file.
using AudioAssets = std::vector<AudioAssetPtr>;

// Min/max of both channels over each block of audio_peak_block frames. The
// last block may be shorter. `right` may be empty.
[[nodiscard]] std::vector<std::pair<float, float>> compute_peaks(std::span<const float> left,
                                                                 std::span<const float> right);

// Decoded files, shared between everything that plays them. Control thread
// only; owned by AppController. Nothing here is safe to call from the audio
// thread.
class AudioAssetCache {
public:
    // Returns the file decoded at `target_rate`: 0 keeps the file's own rate
    // (the sampler plays at a ratio), anything else resamples to it (clips
    // play sample-exact at the engine rate). A second load of the same file at
    // the same rate returns the same asset without decoding again, unless the
    // file changed on disk since.
    //
    // When `expect` is set and the file's frames, rate or channel count differ
    // from it, the file is reported as missing (null, with a reason): a file
    // replaced behind the project's back is never played wrong.
    AudioAssetPtr load(const std::filesystem::path& file, double target_rate,
                       std::optional<AudioFileInfo> expect, std::string* error = nullptr);

    // Registers audio that already exists in memory, such as a recorded take,
    // under the path it is (or will be) written to. Later loads of that path
    // at the asset's own rate, or at 0, return it without reading the file.
    void insert(const std::filesystem::path& file, AudioAsset asset);

    // Registers a file decoded somewhere else, such as an import decoded off
    // the control thread by a cache of its own, so the next load() of that
    // file at the asset's rate returns it without decoding again. `native` is
    // the file's own header. The file's size and write time are taken now, so
    // a later change on disk is noticed exactly as it is for load().
    void adopt(const std::filesystem::path& file, const AudioFileInfo& native,
               AudioAssetPtr asset);

    // Audio derived from a file rather than decoded from it, such as a warped
    // clip's rendition (item 3.6), under a key that names everything it was
    // made from (WarpPlan::key: file, stretch map, pitch, engine rate). Null
    // when nothing is held under `key`.
    [[nodiscard]] AudioAssetPtr derived(const std::string& key) const;
    void insert_derived(const std::string& key, AudioAssetPtr asset);
    [[nodiscard]] std::size_t derived_count() const noexcept { return derived_.size(); }

    // Drops every asset, decoded or derived, nothing outside the cache still
    // holds.
    void purge_unused();
    // Drops only the derived assets nothing outside the cache still holds,
    // such as the renditions of a clip's earlier warp settings.
    void purge_unused_derived();

    // Files decoded since construction. Lets callers (and tests) see that a
    // cache hit did not decode again.
    [[nodiscard]] std::uint64_t decode_count() const noexcept { return decodes_; }
    // Assets currently held, counting each (file, rate) pair once.
    [[nodiscard]] std::size_t size() const noexcept;

private:
    struct Stamp {
        std::uintmax_t size = 0;
        std::filesystem::file_time_type written{};
        friend bool operator==(const Stamp&, const Stamp&) = default;
    };
    struct Entry {
        AudioFileInfo native;
        std::optional<Stamp> stamp;   // nullopt: inserted from memory, trusted
        std::map<std::uint32_t, AudioAssetPtr> by_rate;   // 0 = native rate
    };

    std::map<std::filesystem::path, Entry> entries_;
    std::map<std::string, AudioAssetPtr> derived_;
    std::uint64_t decodes_ = 0;
};

} // namespace blokkily
