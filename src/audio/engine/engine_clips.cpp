#include "engine_clips.hpp"

#include "blokkily/audio/disk_stream.hpp"
#include "blokkily/audio/mixer.hpp"

#include <algorithm>
#include <cmath>

namespace blokkily::engine {

namespace {

std::uint64_t to_engine_frames(std::uint64_t frames, double ratio) {
    return static_cast<std::uint64_t>(std::llround(static_cast<double>(frames) * ratio));
}

} // namespace

void compile_clip_regions(ArrangementClips& target, const Song& song, const TickClock& clock,
                          const AudioAssets& assets, const ClipRenditions& renditions) {
    // Replacing `held` drops whatever the slot's previous arrangement owned.
    // This runs on the control thread, into a slot the callback is neither
    // playing nor about to pick up, so the last owner of a retired buffer
    // lets go of it here and never on the audio thread.
    target.held = assets;
    target.tracks.resize(song.tracks.size());
    for (auto& regions : target.tracks) regions.clear();

    const auto engine_rate = static_cast<std::uint32_t>(std::lround(clock.sample_rate()));
    for (const auto& clip : song.audio_clips) {
        if (clip.track >= song.tracks.size() || clip.file >= song.audio_files.size()) continue;
        if (clip.file >= assets.size() || !assets[clip.file]) continue;   // missing
        const AudioAsset& asset = *assets[clip.file];
        const auto file_rate = song.audio_files[clip.file].sample_rate;
        if (asset.rate != engine_rate || file_rate == 0 || asset.left.empty()) continue;

        if (clip.warp.active()) {
            // A warped clip plays its rendition from its first frame, or
            // nothing while the rendition is still being rendered.
            const auto plan = plan_clip_warp(song, clip, clock, asset.frames);
            if (!plan) continue;
            const auto found = renditions.find(plan->key());
            if (found == renditions.end() || !found->second) continue;
            const AudioAsset& rendition = *found->second;
            if (rendition.rate != engine_rate || rendition.left.size() < plan->output_frames)
                continue;
            ClipRegion region;
            region.start = sample_for_tick(clock, static_cast<double>(clip.start));
            region.frames = plan->output_frames;
            region.left = rendition.left.data();
            region.right = rendition.right.size() == rendition.left.size()
                               ? rendition.right.data() : region.left;
            region.gain = static_cast<float>(db_to_linear(clip.gain_db));
            // The fades are frames of the file; they end and begin where the
            // warp puts those frames.
            const double to_engine = static_cast<double>(engine_rate) / file_rate;
            const auto fade_in_at = plan->output_at(
                std::min(static_cast<double>(plan->source_frames),
                         static_cast<double>(clip.fade_in_frames) * to_engine));
            const auto fade_out_at = plan->output_at(std::max(
                0.0, static_cast<double>(plan->source_frames) -
                         static_cast<double>(clip.fade_out_frames) * to_engine));
            region.fade_in = std::min<std::uint64_t>(
                region.frames, static_cast<std::uint64_t>(std::llround(std::max(0.0, fade_in_at))));
            region.fade_out = std::min<std::uint64_t>(
                region.frames - region.fade_in,
                region.frames - std::min<std::uint64_t>(
                                    region.frames, static_cast<std::uint64_t>(std::llround(
                                                       std::max(0.0, fade_out_at)))));
            target.tracks[clip.track].push_back(region);
            // Held with the files, and let go the same way (on this thread).
            target.held.push_back(found->second);
            continue;
        }

        // The clip counts frames at the file's own rate; the asset holds it
        // resampled to the engine's.
        const double ratio = static_cast<double>(engine_rate) / file_rate;
        const auto offset = to_engine_frames(clip.offset_frames, ratio);
        if (offset >= asset.frames) continue;
        const auto available = asset.frames - offset;
        ClipRegion region;
        region.start = sample_for_tick(clock, static_cast<double>(clip.start));
        region.frames = std::min(available, to_engine_frames(clip.length_frames, ratio));
        if (region.frames == 0) continue;
        region.left = asset.left.data() + offset;
        region.right = asset.right.size() == asset.left.size() ? asset.right.data() + offset
                                                               : region.left;
        region.gain = static_cast<float>(db_to_linear(clip.gain_db));
        region.fade_in = std::min(region.frames, to_engine_frames(clip.fade_in_frames, ratio));
        region.fade_out = std::min(region.frames - region.fade_in,
                                   to_engine_frames(clip.fade_out_frames, ratio));
        target.tracks[clip.track].push_back(region);
    }
    for (auto& regions : target.tracks)
        std::stable_sort(regions.begin(), regions.end(),
                         [](const ClipRegion& a, const ClipRegion& b) { return a.start < b.start; });
}

void sum_clip_regions(ClipPlayback& playback, const ArrangementClips& clips, std::size_t track,
                      StereoBlock buffer, std::uint64_t song_position,
                      bool from_timeline) noexcept {
    // Clips play from the arrangement, so a stopped song hears none of them.
    if (!from_timeline) return;
    if (playback.test_source != nullptr)
        playback.test_source(playback.test_context, buffer, song_position);
    if (playback.stream != nullptr)
        playback.stream->read_and_sum(buffer);
    if (track >= clips.tracks.size()) return;

    const auto frames = std::min(buffer.left.size(), buffer.right.size());
    const auto end = song_position + frames;
    for (const ClipRegion& region : clips.tracks[track]) {
        // Sorted by start: nothing after this one begins inside the chunk.
        if (region.start >= end) break;
        const auto region_end = region.start + region.frames;
        if (region_end <= song_position) continue;
        const auto from = std::max(song_position, region.start);
        const auto to = std::min(end, region_end);
        // Linear fades, from silence on the first frame of the clip and to
        // silence on its last. Division only, never pow or trig, on this
        // thread.
        const double fade_in = static_cast<double>(region.fade_in);
        const double fade_out = static_cast<double>(region.fade_out);
        const auto fade_out_from = region.frames - region.fade_out;
        for (auto sample = from; sample < to; ++sample) {
            const auto frame = sample - region.start;
            double gain = region.gain;
            if (frame < region.fade_in) gain *= static_cast<double>(frame) / fade_in;
            if (region.fade_out > 0 && frame >= fade_out_from)
                gain *= static_cast<double>(region.frames - 1 - frame) / fade_out;
            const auto at = static_cast<std::size_t>(sample - song_position);
            // Summed, never assigned: overlapping clips all sound.
            buffer.left[at] += static_cast<float>(region.left[frame] * gain);
            buffer.right[at] += static_cast<float>(region.right[frame] * gain);
        }
    }
}

} // namespace blokkily::engine
