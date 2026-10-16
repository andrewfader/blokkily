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

} // namespace blokkily
