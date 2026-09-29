#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/audio_clips.hpp"
#include "blokkily/audio/disk_stream.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <filesystem>
#include <vector>

// A streamed clip (wave 4.2) plays through SongEngine::process() without
// allocating: reading its ring, fading on a dry ring and on a locate, and
// cueing it ahead of the song's wrap. The rings are filled between calls, on
// this thread, as a worker would fill them.

namespace {

using namespace blokkily;
using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;
constexpr double rate = 48000.0;

BLOKKILY_REALTIME_CASE(disk_streaming) {
    const std::filesystem::path dir = std::filesystem::path(BLOKKILY_REALTIME_WORK) / "disk_streaming";
    std::filesystem::create_directories(dir);
    const auto file = dir / "stream_48k.wav";
    constexpr std::uint64_t frames = calls * block;
    {
        std::vector<float> l(frames), r(frames);
        for (std::uint64_t i = 0; i < frames; ++i) {
            l[i] = static_cast<float>((i % 200) + 1) / 400.0F;
            r[i] = -l[i];
        }
        WaveWriter writer;
        std::string error;
        require(writer.open(file, static_cast<std::uint32_t>(rate), WaveFormat::float32, &error) &&
                    writer.write(l, r, &error) && writer.close(&error),
                "write test wave: " + error);
    }

    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks = {Track{}};
    song.clips = {};
    song.audio_files.push_back({file, frames, static_cast<std::uint32_t>(rate), 2});
    AudioClip clip;
    clip.id = song.next_audio_clip_id();
    clip.file = 0;
    clip.length_frames = frames / 2;
    song.audio_clips.push_back(clip);

    AudioAssetCache cache;
    cache.set_stream_threshold(0);
    const auto assets = load_clip_assets(song, cache, rate);
    require(!assets.empty() && assets[0] && assets[0]->is_streamed(), "the clip streams");
    SongEngine engine;
    engine.set_disk_stream_workers(0);
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error, assets), "engine prepare: " + error);
    engine.set_playing(true);

    std::vector<float> left(calls * block, -1.0F);
    std::vector<float> right(calls * block, -1.0F);
    test::AllocationCount total;
    for (std::size_t call = 0; call < calls; ++call) {
        // The disk answers except for a stretch long enough to run dry.
        if (call < 300 || call > 700) (void)engine.service_disk_streams();
        if (call == 800) engine.seek(12345);
        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        test::AllocationGuard guard;
        engine.process({out_left, out_right});
        total += guard.count();
    }
    require_no_allocations(total, "1000 SongEngine::process calls playing a streamed clip");
    require(probe::peak(left) > 0.3F, "the streamed clip is heard");
    require(probe::peak(right) > 0.3F, "on both channels");
}

} // namespace
