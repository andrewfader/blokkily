#include "blokkily/audio/audio_asset.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace blokkily {
namespace {

bool fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

// One spelling per file, so "a/../b.wav" and "b.wav" share an entry.
std::filesystem::path key_of(const std::filesystem::path& file) {
    std::error_code failure;
    auto canonical = std::filesystem::weakly_canonical(file, failure);
    if (failure) return file.lexically_normal();
    return canonical;
}

std::string describe(const AudioFileInfo& info) {
    return std::to_string(info.frames) + " frames at " + std::to_string(info.sample_rate) +
           " Hz in " + std::to_string(info.channels) + " channel(s)";
}

std::uint64_t scaled(std::uint64_t frame, double ratio) {
    return static_cast<std::uint64_t>(std::llround(static_cast<double>(frame) * ratio));
}

} // namespace

std::vector<std::pair<float, float>> compute_peaks(std::span<const float> left,
                                                   std::span<const float> right) {
    std::vector<std::pair<float, float>> peaks;
    peaks.reserve((left.size() + audio_peak_block - 1) / audio_peak_block);
    for (std::size_t start = 0; start < left.size(); start += audio_peak_block) {
        const std::size_t end = std::min(left.size(), start + audio_peak_block);
        float low = left[start];
        float high = left[start];
        for (std::size_t frame = start; frame < end; ++frame) {
            low = std::min(low, left[frame]);
            high = std::max(high, left[frame]);
            if (frame < right.size()) {
                low = std::min(low, right[frame]);
                high = std::max(high, right[frame]);
            }
        }
        peaks.emplace_back(low, high);
    }
    return peaks;
}

std::size_t AudioAssetCache::size() const noexcept {
    std::size_t count = 0;
    for (const auto& [path, entry] : entries_) count += entry.by_rate.size();
    return count;
}

AudioAssetPtr AudioAssetCache::load(const std::filesystem::path& file, double target_rate,
                                    std::optional<AudioFileInfo> expect, std::string* error) {
    if (!std::isfinite(target_rate) || target_rate < 0.0 || target_rate > 1.0e6) {
        fail(error, "cannot load " + file.string() + ": the target sample rate is not valid");
        return nullptr;
    }
    const auto key = key_of(file);
    const auto requested = static_cast<std::uint32_t>(std::lround(target_rate));

    // A file that changed on disk since it was decoded is decoded again. An
    // asset inserted from memory has no stamp and is trusted as it is.
    std::optional<Stamp> stamp;
    {
        std::error_code failure;
        const auto size = std::filesystem::file_size(key, failure);
        const auto written = failure ? std::filesystem::file_time_type{}
                                     : std::filesystem::last_write_time(key, failure);
        if (!failure) stamp = Stamp{size, written};
    }
    if (auto found = entries_.find(key); found != entries_.end()) {
        Entry& entry = found->second;
        if (entry.stamp.has_value() && entry.stamp != stamp) {
            entries_.erase(found);
        } else {
            if (expect.has_value() && *expect != entry.native) {
                fail(error, file.string() + " is missing: the project expects " +
                                describe(*expect) + " but the file has " + describe(entry.native));
                return nullptr;
            }
            const std::uint32_t rate = requested == entry.native.sample_rate ? 0 : requested;
            if (auto asset = entry.by_rate.find(rate); asset != entry.by_rate.end()) {
                return asset->second;
            }
        }
    }

    // Check the header before paying for a decode that would be refused.
    if (expect.has_value()) {
        const auto info = probe_audio_file(key, error);
        if (!info.has_value()) return nullptr;
        if (*info != *expect) {
            fail(error, file.string() + " is missing: the project expects " + describe(*expect) +
                            " but the file has " + describe(*info));
            return nullptr;
        }
    }

    auto decoded = decode_audio_file(key, error);
    ++decodes_;
    if (!decoded.has_value()) return nullptr;

    auto asset = std::make_shared<AudioAsset>();
    const std::uint32_t native_rate = decoded->info.sample_rate;
    const std::uint32_t rate = requested == 0 || requested == native_rate ? 0 : requested;
    if (rate == 0) {
        asset->rate = native_rate;
        asset->left = std::move(decoded->left);
        asset->right = std::move(decoded->right);
        asset->loop = decoded->loop;
    } else {
        const double ratio = static_cast<double>(rate) / native_rate;
        asset->rate = rate;
        asset->left = resample(decoded->left, ratio);
        if (!decoded->right.empty()) asset->right = resample(decoded->right, ratio);
        if (decoded->loop.has_value()) {
            asset->loop = LoopPoints{scaled(decoded->loop->start, ratio),
                                     scaled(decoded->loop->end, ratio)};
        }
    }
    asset->frames = asset->left.size();
    asset->root_key = decoded->root_key;
    asset->peaks = compute_peaks(asset->left, asset->right);

    Entry& entry = entries_[key];
    entry.native = decoded->info;
    entry.stamp = stamp;
    entry.by_rate[rate] = asset;
    return asset;
}

void AudioAssetCache::insert(const std::filesystem::path& file, AudioAsset asset) {
    asset.frames = asset.left.size();
    if (asset.right.size() != asset.left.size()) asset.right.clear();
    if (asset.peaks.empty()) asset.peaks = compute_peaks(asset.left, asset.right);
    const AudioFileInfo native{asset.frames, asset.rate,
                               static_cast<std::uint16_t>(asset.right.empty() ? 1 : 2)};
    Entry& entry = entries_[key_of(file)];
    entry = Entry{};
    entry.native = native;
    entry.by_rate[0] = std::make_shared<const AudioAsset>(std::move(asset));
}

void AudioAssetCache::adopt(const std::filesystem::path& file, const AudioFileInfo& native,
                            AudioAssetPtr asset) {
    if (!asset) return;
    const auto key = key_of(file);
    std::optional<Stamp> stamp;
    {
        std::error_code failure;
        const auto size = std::filesystem::file_size(key, failure);
        const auto written = failure ? std::filesystem::file_time_type{}
                                     : std::filesystem::last_write_time(key, failure);
        if (!failure) stamp = Stamp{size, written};
    }
    Entry& entry = entries_[key];
    if (entry.native != native || entry.stamp != stamp) entry = Entry{};
    entry.native = native;
    entry.stamp = stamp;
    const std::uint32_t rate = asset->rate == native.sample_rate ? 0 : asset->rate;
    entry.by_rate[rate] = std::move(asset);
}

void AudioAssetCache::purge_unused() {
    for (auto entry = entries_.begin(); entry != entries_.end();) {
        std::erase_if(entry->second.by_rate,
                      [](const auto& held) { return held.second.use_count() <= 1; });
        entry = entry->second.by_rate.empty() ? entries_.erase(entry) : std::next(entry);
    }
}

} // namespace blokkily
