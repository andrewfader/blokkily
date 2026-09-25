#pragma once

// Where played notes go (plan item 2.5; decisions 3 and 4). One per-track R
// arms a track for notes and audio. The notes of a MIDI keyboard and of the
// on-screen surfaces reach every track armed for notes; with no track armed
// for notes they reach the selected track, as they always have. A track's MIDI
// channel narrows what a keyboard sends it; the on-screen surfaces have no
// channel and reach every armed track.

#include "blokkily/audio/event_queue.hpp"
#include "blokkily/midi/midi_input.hpp"
#include "blokkily/model/song.hpp"

#include <cstddef>

namespace blokkily {

// Whether a track records and plays notes from an input: armed, with an input
// that carries MIDI, and within the tracks an input can reach.
[[nodiscard]] bool armed_for_notes(const Song& song, std::size_t track) noexcept;

// The tracks the on-screen surfaces play.
[[nodiscard]] TrackMask surface_routes(const Song& song, std::size_t selected_track) noexcept;

// The tracks each MIDI channel plays.
[[nodiscard]] ChannelRoutes midi_routes(const Song& song, std::size_t selected_track) noexcept;

} // namespace blokkily
