#pragma once

// Audio input on a track (item 3.2, feature 4c): which device inputs a track
// hears, whether it is heard (monitored), and whether a running take records
// it. Control thread; the engine is handed the result (SongEngine::
// set_audio_input) and reads it on the audio thread.

#include "blokkily/model/song.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace blokkily {

struct AudioInputRoute {
    std::uint16_t first_channel = 0; // 0-based device input channel
    std::uint8_t channels = 0;       // 0: the track takes no audio input; 1 mono; 2 a pair
    bool monitor = false;            // the input is added to the track, playing or not
    bool capture = false;            // recorded while the song plays and records

    friend bool operator==(const AudioInputRoute&, const AudioInputRoute&) = default;
};

// Every track's route, indexed like song.tracks. A track whose input source
// includes audio hears `audio_channels` inputs from `audio_first_channel`.
// It records when it is armed, or, with no track of the song armed, when it
// is the selected track (decision 4). It is monitored when its monitor is on,
// or automatic and the track is armed; a monitored input is the raw input,
// before the track's inserts and strip, exactly as a take records it.
[[nodiscard]] std::vector<AudioInputRoute> audio_input_routes(const Song& song,
                                                              std::size_t selected);

// Whether `input` takes audio from the device at all.
[[nodiscard]] bool takes_audio(const TrackInput& input) noexcept;

} // namespace blokkily
