#pragma once

// The metronome and the count-in (item 3.7, decision 14), private to
// SongEngine.
//
// The click is not a song track. Its beats are compiled with each arrangement
// from the song's meter map and placed by the same tick clock, and the same
// rounding, as every note (sample_for_tick). The render callback adds the
// click to the output after the master strip, through a level of its own, so
// no track's solo or mute, no insert and no fader reaches it. It is placed by
// what the listener hears: the song leaves the engine output_latency() samples
// after it is rendered (plugin delay compensation, decision 10), so a click
// sounds with the beat it belongs to rather than ahead of it.
//
// A count-in is N bars of click before the song starts, in the tempo and meter
// at the place the song starts from. While it plays the playhead does not
// move, nothing is captured, and the notes played into it are tracked so the
// ones still held when the song starts are recorded as beginning there.

#include "blokkily/audio/event_queue.hpp"
#include "blokkily/model/timebase.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace blokkily::engine {

// One beat of the song's click, at the sample its tick sounds on.
struct Click {
    std::uint64_t sample = 0;
    bool accent = false;   // the first beat of a bar
};

// Every click of one compiled arrangement, in sample order.
struct ArrangementClicks {
    std::vector<Click> clicks;
};

// Fills `target` with every beat of `meter` that falls before `song_length`,
// on the beat unit of the bar it is in, placed by `clock`. Control thread.
void compile_clicks(ArrangementClicks& target, const MeterMap& meter, const TickClock& clock,
                    Tick song_length, std::uint64_t song_samples);

// A note played into the count-in, held until its release arrives.
struct HeldNote {
    bool held = false;
    double velocity = 0.0;
    double cents = 0.0;
};

struct MetronomePlayback {
    // The two click sounds at full level, built by prepare_metronome().
    std::vector<float> accent;
    std::vector<float> beat;

    // Set by the control thread, read by the callback.
    std::atomic<bool> enabled{false};
    std::atomic<float> gain{0.5F};
    // Bars of count-in the next start of the transport asks for; the callback
    // takes it as it starts playing.
    std::atomic<std::uint32_t> count_in_request{0};
    // Published by the callback: a count-in is playing and the song has not
    // started yet.
    std::atomic<bool> counting_in{false};

    // --- The render callback's own state --------------------------------
    // The click sounding now, and how far into it the output has got.
    const float* voice = nullptr;
    std::size_t voice_length = 0;
    std::size_t voice_at = 0;
    // Samples the song has rolled since it last started or was sought. The
    // first output_latency() of them still carry what the compensation held
    // back, not the song, so the song's click waits for them.
    std::uint64_t rolled = 0;
    bool was_rolling = false;
    // The count-in: its clicks are placed from the moment it began.
    bool count_clicking = false;   // count-in clicks still to sound
    bool song_waiting = false;     // the song has not started yet
    std::uint64_t count_elapsed = 0;
    std::uint64_t song_starts_at = 0;
    double count_beat = 0.0;
    std::uint32_t count_beats = 0;
    std::uint32_t count_beats_per_bar = 1;
    std::uint32_t next_count_beat = 0;
    // Per track and key, what was played into the count-in and not released.
    std::vector<HeldNote> held;
    std::size_t held_tracks = 0;
};

// Builds the click sounds for `sample_rate` and room to track notes held on
// `tracks` tracks. Control thread, before the callback runs.
void prepare_metronome(MetronomePlayback& metronome, double sample_rate, std::size_t tracks);

// Forgets the click sounding, any count-in and the notes it held.
void reset_metronome(MetronomePlayback& metronome) noexcept;

// Starts a count-in of `bars` bars of `beats_per_bar` beats of `beat_samples`
// samples each. The song starts `latency` samples before the count-in ends,
// so what it plays first is heard just as the last count-in bar ends.
void begin_count_in(MetronomePlayback& metronome, std::uint32_t bars,
                    std::uint32_t beats_per_bar, double beat_samples,
                    std::uint32_t latency) noexcept;

// Samples of count-in left before the song starts.
[[nodiscard]] std::uint64_t count_in_remaining(const MetronomePlayback& metronome) noexcept;

// Notes what the input delivered while counting in: a note-on holds its key
// on its track, a note-off lets it go.
void hold_count_in_notes(MetronomePlayback& metronome,
                         std::span<const RoutedEvent> incoming) noexcept;

// Where the song is for the song's click in one chunk.
struct SongClicks {
    const ArrangementClicks* clicks = nullptr;
    // The song sample the listener hears at the chunk's first frame.
    std::uint64_t heard_start = 0;
    std::uint64_t song_samples = 0;
    std::uint32_t latency = 0;
    bool rolling = false;
};

// Adds the click into one chunk of output: the one already sounding, any
// count-in click due, and, when the song rolls with the click on, every beat
// the listener reaches in it. Render callback.
void render_clicks(MetronomePlayback& metronome, StereoBlock output,
                   const SongClicks& song) noexcept;

} // namespace blokkily::engine
