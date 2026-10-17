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

namespace {

// One file a render writes: the master (no source) or a bus's tap.
struct Target {
    std::optional<OutputTap> source;
    std::filesystem::path file;
    AudioFileWriter writer;
    BounceReport report;
    std::uint64_t latency = 0;
};

// The render every bounce is: the song played once through `engine`, from
// silence, each target written from the block it hears.
bool render(SongEngine& engine, std::vector<Target>& targets, WaveFormat format,
            std::uint64_t tail_frames, const BounceOptions& options, std::string* error) {
    const auto fail = [error](const std::string& message) {
        if (error != nullptr) *error = message;
        return false;
    };
    if (engine.song_samples() == 0 || engine.maximum_block() == 0)
        return fail("prepare the song before bouncing it");
    if (engine.sample_rate() <= 0.0) return fail("song has no sample rate");
    const auto song_end = engine.song_samples();
    const auto start = options.start_sample;
    const auto end = options.end_sample.value_or(song_end);
    if (start >= end || end > song_end) return fail("the range is not inside the song");

    std::vector<OutputTap> taps;
    for (auto& target : targets) {
        if (target.source && !engine.valid_tap(*target.source)) return fail("invalid output source");
        if (target.source) taps.push_back(*target.source);
    }
    std::string writer_error;
    for (std::size_t index = 0; index < targets.size(); ++index) {
        auto& target = targets[index];
        target.writer.set_dither(options.dither);
        if (!target.writer.open(target.file,
                                static_cast<std::uint32_t>(std::llround(engine.sample_rate())),
                                options.container, format, &writer_error)) {
            for (std::size_t opened = 0; opened < index; ++opened) {
                (void)targets[opened].writer.close(nullptr);
                std::error_code ignored;
                std::filesystem::remove(targets[opened].file, ignored);
            }
            return fail(writer_error);
        }
    }

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
    engine.set_bounce_tap(std::nullopt);
    engine.set_stem_taps(taps);
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
    // session's. A bus's tap never carries it.
    const bool had_metronome = engine.metronome_enabled();
    engine.set_metronome_enabled(options.include_metronome);
    // The arrangement loop is for playback. An export is the whole song (or
    // the stretch asked for), the mix that plays from its start with the
    // loop off.
    const bool had_loop = engine.loop_enabled();
    engine.set_loop_enabled(false);
    // Streamed clips (wave 4.2) are read from disk on this thread before
    // they are played: an export never waits on, or is short of, a worker.
    const bool was_blocking = engine.blocking_disk_reads();
    engine.set_blocking_disk_reads(true);
    // Nothing goes to outboard gear while the export renders faster than
    // real time: no notes, no clock.
    const bool midi_was_muted = engine.hardware_midi_muted();
    engine.set_hardware_midi_muted(true);
    engine.set_playing(true);
    engine.seek(start);
    // The export is the song from silence: whatever playback left in the
    // processors, the insert delays and the compensation lines (an echo, a
    // reverb tail, a held voice) is forgotten before the first block, so
    // the file is what a freshly prepared engine renders.
    engine.reset_processing();

    const auto block = static_cast<std::size_t>(engine.maximum_block());
    std::vector<float> left(block, 0.0F);
    std::vector<float> right(block, 0.0F);
    // The speakers hear the song `latency` samples late, however many
    // latent inserts it passes through (decision 10). The file starts where
    // the song does: the first `latency` frames the engine renders are the
    // compensation, not the song, and are dropped. An insert that keeps
    // sounding after its input stops (an echo, a reverb) gets its whole tail.
    std::uint64_t longest_latency = 0;
    for (auto& target : targets) {
        target.latency = target.source ? engine.tap_latency(*target.source) : engine.output_latency();
        longest_latency = std::max(longest_latency, target.latency);
    }
    const std::uint64_t length = end - start;
    const std::uint64_t tail = std::max(tail_frames, engine.effect_tail_samples());
    const std::uint64_t total = length + tail;
    std::uint64_t rendered = 0;
    std::uint64_t progressed = 0;
    bool wrote = true;
    bool cancelled = false;
    while (rendered < longest_latency + total && wrote) {
        // Finish the stretch exactly once, then let the same instruments and
        // effects render their releases with the transport stopped. Never
        // wrap into the first bar just because the export requested a tail.
        if (rendered == length) engine.set_playing(false);
        const auto remaining = rendered < length ? length - rendered
                                                 : longest_latency + total - rendered;
        const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>(block, remaining));
        const std::span<float> left_block{left.data(), frames};
        const std::span<float> right_block{right.data(), frames};
        engine.process({left_block, right_block});
        std::size_t tap = 0;
        for (auto& target : targets) {
            const auto heard_left = target.source ? engine.stem_left(tap).first(frames)
                                                  : std::span<const float>{left_block};
            const auto heard_right = target.source ? engine.stem_right(tap).first(frames)
                                                   : std::span<const float>{right_block};
            if (target.source) ++tap;
            // Whatever part of this block is still the compensation is skipped.
            const auto skip = static_cast<std::size_t>(
                rendered >= target.latency ? 0
                                           : std::min<std::uint64_t>(frames, target.latency - rendered));
            if (skip == frames || target.report.frames >= total) continue;
            const auto kept = std::min<std::uint64_t>(frames - skip, total - target.report.frames);
            const auto kept_left = heard_left.subspan(skip, static_cast<std::size_t>(kept));
            const auto kept_right = heard_right.subspan(skip, static_cast<std::size_t>(kept));
            for (std::size_t frame = 0; frame < kept_left.size(); ++frame)
                target.report.peak = std::max({target.report.peak, std::abs(kept_left[frame]),
                                               std::abs(kept_right[frame])});
            wrote = target.writer.write(kept_left, kept_right, &writer_error) && wrote;
            target.report.frames += kept;
        }
        rendered += frames;
        std::uint64_t least = total;
        for (const auto& target : targets) least = std::min(least, target.report.frames);
        if (options.progress && least != progressed) options.progress(least, total);
        progressed = least;
        if (options.cancelled && options.cancelled()) {
            cancelled = true;
            break;
        }
    }

    // The export's own tail is not left ringing into the session: playback
    // resumes from silence at the place, and in the state, it was left in.
    engine.reset_processing();
    bool restored = true;
    for (auto& [instance, state] : states)
        if (!state.empty()) restored = instance->load_state(state) && restored;
    engine.set_stem_taps({});
    engine.set_bounce_tap(previous_tap);
    engine.set_blocking_disk_reads(was_blocking);
    engine.set_hardware_midi_muted(midi_was_muted);
    engine.set_metronome_enabled(had_metronome);
    engine.set_loop_enabled(had_loop);
    engine.set_playing(was_playing);
    engine.seek(resume_at);
    engine.set_recording(was_recording);
    engine.connect_input(input);
    bool closed = true;
    for (auto& target : targets) closed = target.writer.close(&writer_error) && closed;
    if (cancelled || !wrote || !closed) {
        for (auto& target : targets) {
            std::error_code ignored;
            std::filesystem::remove(target.file, ignored);
        }
        return fail(cancelled ? std::string("cancelled") : writer_error);
    }
    if (!restored) return fail("a processor could not restore its state after export");
    for (auto& target : targets) target.report.clipped = target.report.peak > 1.0F;
    return true;
}

} // namespace

std::optional<BounceReport> bounce_song(SongEngine& engine, const std::filesystem::path& file,
                                        WaveFormat format, std::uint64_t tail_frames,
                                        const BounceOptions& options, std::string* error) {
    std::vector<Target> targets(1);
    targets[0].source = options.source;
    targets[0].file = file;
    if (!render(engine, targets, format, tail_frames, options, error)) return std::nullopt;
    return targets[0].report;
}

bool bounce_taps(SongEngine& engine, std::vector<TapFile>& taps, WaveFormat format,
                 std::uint64_t tail_frames, const BounceOptions& options, std::string* error) {
    std::vector<Target> targets(taps.size());
    for (std::size_t index = 0; index < taps.size(); ++index) {
        targets[index].source = taps[index].source;
        targets[index].file = taps[index].file;
    }
    BounceOptions stems = options;
    stems.include_metronome = false;
    if (!render(engine, targets, format, tail_frames, stems, error)) return false;
    for (std::size_t index = 0; index < taps.size(); ++index) taps[index].report = targets[index].report;
    return true;
}

std::string stem_file_name(std::size_t index, const std::string& name) {
    std::string clean;
    for (const char c : name) {
        const auto u = static_cast<unsigned char>(c);
        clean += (u < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                  c == '"' || c == '<' || c == '>' || c == '|')
                     ? ' ' : c;
    }
    while (!clean.empty() && (clean.back() == ' ' || clean.back() == '.')) clean.pop_back();
    while (!clean.empty() && clean.front() == ' ') clean.erase(clean.begin());
    if (clean.empty()) clean = "Track";
    const auto number = std::to_string(index + 1);
    return std::string(number.size() < 2 ? 2 - number.size() : 0, '0') + number + " " + clean;
}

std::optional<std::vector<StemFile>> bounce_stems(
    SongEngine& engine, const Song& song, const std::filesystem::path& directory,
    WaveFormat format, std::uint64_t tail_frames, const BounceOptions& options,
    std::string* error) {
    std::vector<StemFile> stems;
    for (std::size_t track = 0; track < song.tracks.size(); ++track)
        stems.push_back({{BusKind::track, static_cast<std::uint32_t>(track)},
                         song.tracks[track].name, {}, {}});
    for (std::size_t ret = 0; ret < song.returns.size(); ++ret)
        stems.push_back({{BusKind::ret, static_cast<std::uint32_t>(ret)},
                         song.returns[ret].name, {}, {}});
    if (stems.empty()) {
        if (error != nullptr) *error = "the song has no tracks";
        return std::nullopt;
    }
    std::vector<TapFile> taps;
    for (std::size_t index = 0; index < stems.size(); ++index) {
        auto& stem = stems[index];
        stem.file = directory / (stem_file_name(index, stem.name) +
                                 container_extension(options.container));
        taps.push_back({stem.source, stem.file, {}});
    }
    BounceOptions all = options;
    if (options.progress) {
        // Told in stem-frames, as a stem at a time would be: every stem is
        // written by the same block.
        const auto count = static_cast<std::uint64_t>(stems.size());
        all.progress = [&options, count](std::uint64_t written, std::uint64_t total) {
            options.progress(written * count, total * count);
        };
    }
    if (!bounce_taps(engine, taps, format, tail_frames, all, error)) return std::nullopt;
    for (std::size_t index = 0; index < stems.size(); ++index) stems[index].report = taps[index].report;
    return stems;
}

} // namespace blokkily
