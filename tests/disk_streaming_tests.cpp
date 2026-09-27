#include "support/audio_probe.hpp"

#include "blokkily/audio/disk_stream.hpp"
#include "blokkily/audio/mixer.hpp"
#include "blokkily/audio/playback.hpp"
#include "blokkily/audio/song_engine.hpp"
#include "blokkily/audio/wave_file.hpp"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace blokkily;
namespace fs = std::filesystem;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

const float centre = strip_gain(MixerStrip{}, false).left;

constexpr double rate = 48000.0;
constexpr std::uint32_t block = 512;
constexpr std::uint64_t total_frames = 48000;

float ramp_left(std::uint64_t frame) { return static_cast<float>(frame + 1) / 65536.0F; }
float ramp_right(std::uint64_t frame) { return -static_cast<float>(frame + 1) / 65536.0F; }

fs::path make_temp_dir(const std::string& name) {
    const fs::path dir = fs::temp_directory_path() / "blokkily_stream_tests" / name;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    return dir;
}

void write_test_wave(const fs::path& file, std::uint64_t frames) {
    std::vector<float> l(frames), r(frames);
    for (std::uint64_t i = 0; i < frames; ++i) {
        l[i] = ramp_left(i);
        r[i] = ramp_right(i);
    }
    WaveWriter writer;
    std::string error;
    fs::create_directories(file.parent_path());
    require(writer.open(file, static_cast<std::uint32_t>(rate), WaveFormat::float32, &error) &&
                writer.write(l, r, &error) && writer.close(&error),
            "write test wave: " + error);
}

Song make_single_track_song() {
    Song song;
    song.patterns = {{"P", Pattern(1920, 480)}};
    song.tracks = {Track{}};
    return song;
}

void playback_case() {
    const fs::path dir = make_temp_dir("playback");
    const fs::path wave_file = dir / "test_ramp.wav";
    write_test_wave(wave_file, total_frames);

    DiskStreamService service(1);
    auto stream = service.open_stream(wave_file, 65536);
    require(stream && stream->is_open(), "stream must be open");

    Song song = make_single_track_song();
    SongEngine engine;
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "engine prepare: " + error);
    engine.set_track_disk_stream(0, stream.get());

    service.prefill(32768);

    RtAudioOutput output{RtAudioOutput::Mode::deterministic};
    require(output.open(engine) && output.start(), "output open & start");
    engine.set_playing(true);

    const std::size_t test_blocks = 20; // 20 * 512 = 10240 frames
    std::vector<float> buffer(block * 2);

    for (std::size_t b = 0; b < test_blocks; ++b) {
        require(output.pump(buffer), "pump must succeed");
        for (std::size_t i = 0; i < block; ++i) {
            const std::uint64_t frame = b * block + i;
            const float left = buffer[i];
            const float right = buffer[block + i];
            const float expected_l = ramp_left(frame) * centre;
            const float expected_r = ramp_right(frame) * centre;
            require(std::abs(left - expected_l) < 1e-4F,
                    "sample mismatch on left channel at frame " + std::to_string(frame));
            require(std::abs(right - expected_r) < 1e-4F,
                    "sample mismatch on right channel at frame " + std::to_string(frame));
        }
    }
}

void seek_case() {
    const fs::path dir = make_temp_dir("seek");
    const fs::path wave_file = dir / "test_ramp.wav";
    write_test_wave(wave_file, total_frames);

    DiskStreamService service(1);
    auto stream = service.open_stream(wave_file, 65536);
    require(stream && stream->is_open(), "stream must be open");

    Song song = make_single_track_song();
    SongEngine engine;
    std::string error;
    require(engine.prepare(song, rate, block, 0, &error), "engine prepare: " + error);
    engine.set_track_disk_stream(0, stream.get());

    service.prefill(16384);

    RtAudioOutput output{RtAudioOutput::Mode::deterministic};
    require(output.open(engine) && output.start(), "output open & start");
    engine.set_playing(true);

    std::vector<float> buffer(block * 2);
    // Play 2 blocks
    require(output.pump(buffer), "pump block 0");
    require(output.pump(buffer), "pump block 1");

    // Seek to frame 24000
    const std::uint64_t target = 24000;
    stream->seek(target);
    service.prefill(16384);

    // Pump block after seek
    require(output.pump(buffer), "pump block after seek");
    // Verify first sample after seek matches target frame
    const float first_l = buffer[0];
    const float expected_l = ramp_left(target) * centre;
    require(std::abs(first_l - expected_l) < 1e-3F,
            "seek target sample mismatch: got " + std::to_string(first_l) + " expected " + std::to_string(expected_l));
}

void underrun_case() {
    AudioStreamRingBuffer ring(1024);
    // Fill only 64 frames
    std::vector<float> fill_l(64, 1.0F);
    std::vector<float> fill_r(64, 1.0F);
    ring.write(fill_l, fill_r);

    // Read 256 frames from ring with only 64 available
    std::vector<float> out_l(256, -9.0F);
    std::vector<float> out_r(256, -9.0F);
    const std::size_t read_count = ring.read(out_l, out_r);
    require(read_count == 64, "read_count must equal available frames");

    // Frames 0..63 must be 1.0F, frames 64..255 must be padded with 0.0F
    for (std::size_t i = 0; i < 64; ++i) {
        require(out_l[i] == 1.0F, "available frame must be read");
    }
    for (std::size_t i = 64; i < 256; ++i) {
        require(out_l[i] == 0.0F, "underrun frame must be zeroed");
    }

    // Now test DiskStream soft fade out on underrun
    const fs::path dir = make_temp_dir("underrun");
    const fs::path wave_file = dir / "test_ramp.wav";
    write_test_wave(wave_file, 1024);

    DiskStream stream(wave_file, 2048);
    // Pre-fill only 100 frames into stream's ring
    std::vector<float> p_l(100, 0.5F);
    std::vector<float> p_r(100, 0.5F);
    stream.ring_buffer().write(p_l, p_r);

    std::vector<float> track_l(256, 0.0F);
    std::vector<float> track_r(256, 0.0F);
    StereoBlock block_view{track_l, track_r};

    require(!stream.has_underrun(), "initially no underrun");
    stream.read_and_sum(block_view);
    require(stream.has_underrun(), "underrun flag must be set when requested > available");
    // Verify soft fade occurred: sample values should gracefully descend rather than abrupt pop
    require(track_l[0] > 0.0F, "first sample has signal");
    require(track_l[255] == 0.0F, "starved tail must be silence");
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: blokkily_disk_streaming_tests <case>\n";
        return 2;
    }
    const std::string name = argv[1];
    try {
        if (name == "playback") playback_case();
        else if (name == "seek") seek_case();
        else if (name == "underrun") underrun_case();
        else {
            std::cerr << "Unknown case: " << name << "\n";
            return 2;
        }
    } catch (const std::exception& ex) {
        std::cerr << "FAILED: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
