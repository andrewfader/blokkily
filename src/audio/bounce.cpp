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
    // A bounce is the arrangement, not whoever is at the keyboard while it
    // renders: live input is held back until it is done and nothing is
    // recorded from it.
    auto* const input = engine.input();
    const bool was_recording = engine.is_recording();
    engine.connect_input(nullptr);
    engine.set_recording(false);
    engine.set_playing(true);
    engine.rewind();

    const auto block = static_cast<std::size_t>(engine.maximum_block());
    std::vector<float> left(block, 0.0F);
    std::vector<float> right(block, 0.0F);
    BounceReport report;
    // The speakers hear the song `latency` samples late, however many
    // latent inserts it passes through (decision 10). The file starts where
    // the song does: the first `latency` frames the engine renders are the
    // compensation, not the song, and are dropped. An insert that keeps
    // sounding after its input stops (an echo, a reverb) gets its whole tail.
    const std::uint64_t latency = engine.output_latency();
    const std::uint64_t tail = std::max(tail_frames, engine.effect_tail_samples());
    const std::uint64_t total = engine.song_samples() + tail;
    std::uint64_t rendered = 0;
    bool wrote = true;
    while (report.frames < total && wrote) {
        // Finish the arrangement exactly once, then let the same instruments
        // and effects render their releases with the transport stopped. Never
        // wrap into the first bar just because the export requested a tail.
        const auto song_end = engine.song_samples();
        if (rendered == song_end) engine.set_playing(false);
        const auto remaining = rendered < song_end ? song_end - rendered
                                                   : latency + total - rendered;
        const auto frames = static_cast<std::size_t>(
            std::min<std::uint64_t>(block, remaining));
        const std::span<float> left_block{left.data(), frames};
        const std::span<float> right_block{right.data(), frames};
        engine.process({left_block, right_block});
        // Whatever part of this block is still the compensation is skipped.
        const auto skip = static_cast<std::size_t>(
            rendered >= latency ? 0 : std::min<std::uint64_t>(frames, latency - rendered));
        rendered += frames;
        if (skip == frames) continue;
        const auto kept = std::min<std::uint64_t>(frames - skip, total - report.frames);
        const auto kept_left = left_block.subspan(skip, static_cast<std::size_t>(kept));
        const auto kept_right = right_block.subspan(skip, static_cast<std::size_t>(kept));
        for (std::size_t frame = 0; frame < kept_left.size(); ++frame)
            report.peak = std::max({report.peak, std::abs(kept_left[frame]),
                                    std::abs(kept_right[frame])});
        wrote = writer.write(kept_left, kept_right, &writer_error);
        report.frames += kept;
    }

    engine.set_playing(was_playing);
    engine.seek(resume_at);
    engine.set_recording(was_recording);
    engine.connect_input(input);
    if (!wrote || !writer.close(&writer_error)) return fail(writer_error.c_str());
    report.clipped = report.peak > 1.0F;
    return report;
}

} // namespace blokkily
