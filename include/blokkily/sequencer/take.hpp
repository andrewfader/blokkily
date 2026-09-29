#pragma once

#include "blokkily/model/song.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace blokkily {

// A note as it was played in against the running song: where it began and how
// long it was held, in song ticks, and what it sounded.
struct PlayedNote {
    Tick start = 0;
    Tick duration = 1;
    std::int16_t key = 60;
    float velocity = 0.8F;
    double cents = 0.0;
    // How it moved while held (MPE): voice 0, offsets from its start.
    std::vector<NoteExpression> expression{};
};

// Pairs the key-downs and key-ups heard during a take into notes. The song
// loops, so a note held across the loop point is released at a position
// earlier than the one it began at; it still lasted as long as it was held.
class TakeRecorder {
public:
    explicit TakeRecorder(Tick song_length = 1920);
    void note_on(Tick at, std::int16_t key, float velocity, double cents);
    // The finished note, or nothing if that key was not held.
    [[nodiscard]] std::optional<PlayedNote> note_off(Tick at, std::int16_t key);
    // Lets go of every key still held at `at`, the way stopping the song does.
    [[nodiscard]] std::vector<PlayedNote> finish(Tick at);
    [[nodiscard]] bool holding() const noexcept { return !held_.empty(); }
    // A controller message heard at song tick `at` (wave 4.1): the movement
    // it is, or nothing for a message a pattern does not keep (see
    // continuous_from_midi in event.hpp).
    [[nodiscard]] std::optional<ContinuousEvent> control(Tick at, std::uint32_t raw) const;
    // Per-note expression heard at song tick `at` for the held note on
    // `key` (MPE): kept with that note, at its offset from the note's start
    // read round the loop. A second value of the same kind on the same tick
    // replaces the first. Nothing for a key not held.
    void expression(Tick at, std::int16_t key, NoteExpression::Kind kind, float value);

private:
    Tick length_;
    std::vector<PlayedNote> held_;
};

// Where a note played at `tick` on `track` belongs: the pattern of the clip
// playing there, at the same place in that repetition. With no clip under the
// playhead it goes into the pattern open in the editors, at the same place in
// its loop, so nothing that was played is thrown away.
struct TakeTarget {
    std::size_t pattern = 0;
    Tick offset = 0;
};
[[nodiscard]] TakeTarget take_target(const Song& song, std::size_t track, Tick tick,
                                     std::size_t open_pattern);

// Writes a played note onto the step nearest where it was played, keeping how
// far from the grid it landed as micro-timing, so the editors show it on its
// step and the song plays it back when it was played. A step that already
// sounds that pitch is left alone; a step that sounds another becomes a chord
// of both, so a take laid over a pattern adds to it rather than erasing it.
// Each voice of that chord keeps its own velocity and its own held length, so
// a soft short key and a hard long one merged onto a step still sound that way.
// `note.start` is in the pattern's own ticks. Returns the step written. A
// played note's per-note expression goes with it: onto its voice of the step,
// the other voices' expression following their voices when a chord is made
// or reordered.
int write_played(Pattern& pattern, PlayedNote note, Tick ticks_per_step);

// Writes a played controller movement into a pattern at `event.tick`, read in
// the pattern's own ticks round its loop. A value of the same controller on
// that tick is replaced: a wheel moved many times within one block keeps
// where it ended up.
void write_continuous(Pattern& pattern, ContinuousEvent event);

} // namespace blokkily
