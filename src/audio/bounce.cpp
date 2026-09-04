#include "blokkily/audio/bounce.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace blokkily {

std::optional<BounceReport> bounce_song(SongEngine& engine, const std::filesystem::path& file,
                                        WaveFormat format, std::uint64_t tail_frames,
                                        std::string* error) {
    const auto fail = [error](const char* message) -> std::optional<BounceReport> {
        if (error != nullptr) *error = message;
        return std::nullopt;
    };
    if (engine.song_samples() == 0 || engine.maximum_block() == 0)
        return fail("prepare the song before bouncing it");
    if (engine.sample_rate() <= 0.0) return fail("song has no sample rate");

    WaveWriter writer;
    std::string writer_error;
    if (!writer.open(file, static_cast<std::uint32_t>(std::llround(engine.sample_rate())),
                     format, &writer_error))
        return fail(writer_error.c_str());

    // Bouncing takes over the transport, then puts it back where it was, so an
    // export cannot leave the session playing from somewhere unexpected.
    const bool was_playing = engine.is_playing();
    const auto resume_at = engine.sample_position();
    engine.set_playing(true);
    engine.rewind();

    const auto block = static_cast<std::size_t>(engine.maximum_block());
    std::vector<float> left(block, 0.0F);
    std::vector<float> right(block, 0.0F);
    BounceReport report;
    const std::uint64_t total = engine.song_samples() + tail_frames;
    bool wrote = true;
    while (report.frames < total && wrote) {
        const auto frames = static_cast<std::size_t>(
            std::min<std::uint64_t>(block, total - report.frames));
        const std::span<float> left_block{left.data(), frames};
        const std::span<float> right_block{right.data(), frames};
        engine.process({left_block, right_block});
        for (std::size_t frame = 0; frame < frames; ++frame)
            report.peak = std::max({report.peak, std::abs(left_block[frame]),
                                    std::abs(right_block[frame])});
        wrote = writer.write(left_block, right_block, &writer_error);
        report.frames += frames;
    }

    engine.set_playing(was_playing);
    engine.seek(resume_at);
    if (!wrote || !writer.close(&writer_error)) return fail(writer_error.c_str());
    report.clipped = report.peak > 1.0F;
    return report;
}

} // namespace blokkily
