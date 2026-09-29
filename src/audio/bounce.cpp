#include "blokkily/audio/bounce.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace blokkily {

std::optional<BounceReport> bounce_song(SongEngine& engine, const std::filesystem::path& file,
                                        WaveFormat format, std::uint64_t tail_frames,
                                        std::string* error) {
    return bounce_song(engine, file, format, tail_frames, BounceOptions{}, error);
}

std::optional<BounceReport> bounce_song(SongEngine& engine, const std::filesystem::path& file,
                                        WaveFormat format, std::uint64_t tail_frames,
                                        const BounceOptions& options, std::string* error) {
    const auto fail = [error](const char* message) -> std::optional<BounceReport> {
        if (error != nullptr) *error = message;
        return std::nullopt;
    };
    if (engine.song_samples() == 0 || engine.maximum_block() == 0)
        return fail("prepare the song before bouncing it");
    if (engine.sample_rate() <= 0.0) return fail("song has no sample rate");

    if (options.source && !engine.valid_tap(*options.source)) return fail("invalid output source");
    WaveWriter writer;
    std::string writer_error;
    if (!writer.open(file, static_cast<std::uint32_t>(std::llround(engine.sample_rate())),
                     format, &writer_error))
        return fail(writer_error.c_str());

    // Bouncing takes over the transport, then puts it back where it was, so an
    // export cannot leave the session playing from somewhere unexpected.
    // Timeline locks and automation change plugin parameters while rendering.
    // Keep the producer's state so an export cannot change the next playback
    // or the next export. State streams run with the device stopped.
    std::vector<std::pair<PluginInstance*, std::vector<std::byte>>> states;
    for (const auto where : engine.processor_addresses())
        if (auto* instance = engine.processor(where))
            states.emplace_back(instance, instance->save_state());
    const auto previous_tap = engine.bounce_tap();
    engine.set_bounce_tap(options.source);
    const bool was_playing = engine.is_playing();
    const auto resume_at = engine.sample_position();
    // A bounce is the arrangement, not whoever is at the keyboard while it
    // renders: live input is held back until it is done and nothing is
    // recorded from it.
    auto* const input = engine.input();
    const bool was_recording = engine.is_recording();
    engine.connect_input(nullptr);
    engine.set_recording(false);
    // The click is a guide for whoever plays along, not part of the mix,
    // unless the export asks for it (decision 14). Its level stays the
    // session's.
    const bool had_metronome = engine.metronome_enabled();
    engine.set_metronome_enabled(options.include_metronome);
    // Streamed clips (wave 4.2) are read from disk on this thread before
    // they are played: an export never waits on, or is short of, a worker.
    const bool was_blocking = engine.blocking_disk_reads();
    engine.set_blocking_disk_reads(true);
    engine.set_playing(true);
    engine.rewind();
    // The export is the song from silence: whatever playback left in the
    // processors, the insert delays and the compensation lines (an echo, a
    // reverb tail, a held voice) is forgotten before the first block, so
    // the file is what a freshly prepared engine renders.
    engine.reset_processing();

    const auto block = static_cast<std::size_t>(engine.maximum_block());
    std::vector<float> left(block, 0.0F);
    std::vector<float> right(block, 0.0F);
    BounceReport report;
    // The speakers hear the song `latency` samples late, however many
    // latent inserts it passes through (decision 10). The file starts where
    // the song does: the first `latency` frames the engine renders are the
    // compensation, not the song, and are dropped. An insert that keeps
    // sounding after its input stops (an echo, a reverb) gets its whole tail.
    const std::uint64_t latency = options.source ? engine.tap_latency(*options.source)
                                                 : engine.output_latency();
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

    // The export's own tail is not left ringing into the session: playback
    // resumes from silence at the place, and in the state, it was left in.
    engine.reset_processing();
    bool restored = true;
    for (auto& [instance, state] : states)
        if (!state.empty()) restored = instance->load_state(state) && restored;
    engine.set_bounce_tap(previous_tap);
    engine.set_blocking_disk_reads(was_blocking);
    engine.set_metronome_enabled(had_metronome);
    engine.set_playing(was_playing);
    engine.seek(resume_at);
    engine.set_recording(was_recording);
    engine.connect_input(input);
    const bool closed = writer.close(&writer_error);
    if (!wrote || !closed) return fail(writer_error.c_str());
    if (!restored) return fail("a processor could not restore its state after export");
    report.clipped = report.peak > 1.0F;
    return report;
}

} // namespace blokkily
