#include "blokkily/audio/song_engine.hpp"
#include "engine/engine_effects.hpp"

#include <algorithm>

namespace blokkily {
bool SongEngine::valid_tap(OutputTap source) const noexcept {
    if (source.pre_fader && source.kind != BusKind::track) return false;
    switch (source.kind) {
    case BusKind::track: return source.bus < tracks_.size();
    case BusKind::ret: return source.bus < buses_->returns.size();
    case BusKind::master: return source.bus == 0;
    }
    return false;
}

std::uint32_t SongEngine::tap_latency(OutputTap source) const noexcept {
    if (!valid_tap(source)) return 0;
    return buses_->track_latency + (source.kind == BusKind::track ? 0 : buses_->return_latency) +
        (source.kind == BusKind::master ? buses_->master.latency : 0);
}

bool SongEngine::configure_output_capture(OutputTap source, SampleRing* ring) noexcept {
    if (!valid_tap(source)) return false;
    capture_source_ = source;
    output_capture_.store(ring, std::memory_order_release);
    return true;
}

void SongEngine::set_stem_taps(std::vector<OutputTap> taps) {
    stem_taps_ = std::move(taps);
    const auto frames = static_cast<std::size_t>(maximum_block_);
    stem_left_.assign(stem_taps_.size(), std::vector<float>(frames, 0.0F));
    stem_right_.assign(stem_taps_.size(), std::vector<float>(frames, 0.0F));
}

void SongEngine::tap_output(OutputTap source, StereoBlock block, std::uint64_t position,
                            bool rolling) noexcept {
    if (bounce_tap_ && *bounce_tap_ == source) {
        std::copy(block.left.begin(), block.left.end(), tap_left_.begin());
        std::copy(block.right.begin(), block.right.end(), tap_right_.begin());
    }
    for (std::size_t index = 0; index < stem_taps_.size(); ++index)
        if (stem_taps_[index] == source &&
            chunk_begin_ + block.left.size() <= stem_left_[index].size()) {
            std::copy(block.left.begin(), block.left.end(), stem_left_[index].begin() + chunk_begin_);
            std::copy(block.right.begin(), block.right.end(),
                      stem_right_[index].begin() + chunk_begin_);
        }
    if (auto* ring = output_capture_.load(std::memory_order_acquire);
        ring != nullptr && source == capture_source_ && rolling && is_recording())
        (void)ring->push(0, position, block.left, block.right);
}
} // namespace blokkily
