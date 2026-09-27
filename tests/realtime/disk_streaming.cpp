#include "realtime_case.hpp"
#include "../support/audio_probe.hpp"

#include "blokkily/audio/disk_stream.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <filesystem>
#include <vector>

namespace {

using namespace blokkily;
using blokkily::realtime::require;
using blokkily::realtime::require_no_allocations;

constexpr std::size_t calls = 1000;
constexpr std::size_t block = 256;
constexpr double rate = 48000.0;

blokkily::Song make_streaming_song() {
    blokkily::Song song;
    song.patterns = {{"P", blokkily::Pattern(1920, 480)}};
    song.tracks = {blokkily::Track{}};
    return song;
}

void write_test_wave(const std::filesystem::path& file, std::uint64_t frames) {
    std::vector<float> l(frames), r(frames);
    for (std::uint64_t i = 0; i < frames; ++i) {
        l[i] = static_cast<float>(i + 1) / 65536.0F;
        r[i] = -static_cast<float>(i + 1) / 65536.0F;
    }
    std::filesystem::create_directories(file.parent_path());
    blokkily::WaveWriter writer;
    std::string error;
    require(writer.open(file, static_cast<std::uint32_t>(rate), blokkily::WaveFormat::float32, &error) &&
                writer.write(l, r, &error) && writer.close(&error),
            "write test wave: " + error);
}

BLOKKILY_REALTIME_CASE(disk_streaming) {
    const std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "blokkily_rt_stream";
    std::filesystem::create_directories(temp_dir);
    const std::filesystem::path wave_file = temp_dir / "stream_48k.wav";
    write_test_wave(wave_file, calls * block * 2);

    blokkily::DiskStreamService service(1);
    auto stream = service.open_stream(wave_file, 65536);
    require(stream && stream->is_open(), "stream must open successfully");

    blokkily::Song song = make_streaming_song();
    blokkily::SongEngine engine;
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "engine prepare: " + error);
    engine.set_track_disk_stream(0, stream.get());
    engine.set_playing(true);

    service.prefill(32768);

    std::vector<float> left(calls * block, -1.0F);
    std::vector<float> right(calls * block, -1.0F);
    test::AllocationCount total;

    for (std::size_t call = 0; call < calls; ++call) {
        const std::span<float> out_left{left.data() + call * block, block};
        const std::span<float> out_right{right.data() + call * block, block};
        {
            test::AllocationGuard guard;
            engine.process({out_left, out_right});
            total += guard.count();
        }
    }

    require_no_allocations(total, "1000 SongEngine::process calls playing disk stream");
    require(probe::peak(left) > 0.001F, "left channel must receive audio from stream");
    require(probe::peak(right) > 0.001F, "right channel must receive audio from stream");

    std::error_code ec;
    std::filesystem::remove_all(temp_dir, ec);
}

} // namespace
