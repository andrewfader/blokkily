#pragma once

// Comping (features/comping_and_punch.feature): several takes of the same
// bars on one track - the passes of a loop recording, say - are a stack, and
// one of them plays at a time. The others are kept, comped out
// (AudioClip::comped_out), and can be chosen instead. Splitting the stack at
// a tick lets each section of the bars come from a different take. Clips
// that overlap without being comped out still sum, as before.

#include "blokkily/model/song.hpp"

#include <cstddef>
#include <vector>

namespace blokkily {

// The clips on `clip`'s track that share any of its time, `clip` included,
// in the order they were recorded (by id): its take stack.
[[nodiscard]] std::vector<AudioClipId> take_stack(const Song& song, AudioClipId clip);

// Makes `clip` the one that plays: every other clip of its stack that
// overlaps it is comped out. False when there is no such clip.
bool choose_take(Song& song, AudioClipId clip);

// After a recording: among the clips it made (`recorded`), where several
// cover the same time on a track, the latest plays and the earlier are
// comped out. Clips the song had before are left as they were. Returns how
// many were comped out.
int comp_recorded_takes(Song& song, const std::vector<AudioClipId>& recorded);

// Cuts every clip on `track` that spans `at` into two, at `at`, keeping
// whether each half is comped out. Each half keeps its own fades away from
// the cut; the cut itself has none, so a take that continues across it is
// seamless. Returns how many clips were cut.
int split_takes(Song& song, std::size_t track, Tick at);

// --- MIDI takes ---------------------------------------------------------------
// A looped MIDI recording makes each pass a take: a take section's part
// (Section::take) placed by a clip of its own over the loop's range. Takes of
// the same bars stack and comp each other as audio takes do; the pattern
// clips they were recorded over are not in the stack and keep playing under
// them, as an overdub does.

// Whether clip `clip` places a take.
[[nodiscard]] bool is_midi_take(const Song& song, std::size_t clip);
// The take clips on `clip`'s track that share any of its time, `clip`
// included, in the order they were recorded (by clip index).
[[nodiscard]] std::vector<std::size_t> midi_take_stack(const Song& song, std::size_t clip);
// Makes take clip `clip` the one that plays: every other take of its stack
// that overlaps it is comped out. False when there is no such take.
bool choose_midi_take(Song& song, std::size_t clip);
// Places pattern `pattern` (a take section's part) on `track` from `start`,
// for its pattern's length, as the take that plays: every earlier take it
// overlaps is comped out. Returns the new clip's index.
std::size_t place_midi_take(Song& song, std::size_t track, std::size_t pattern, Tick start);
// Cuts every take clip on `track` that spans `at` into two, at `at`, each half
// keeping whether it is comped out. Returns how many were cut.
int split_midi_takes(Song& song, std::size_t track, Tick at);

} // namespace blokkily
