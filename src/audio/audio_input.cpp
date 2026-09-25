#include "blokkily/audio/audio_input.hpp"

#include <algorithm>

namespace blokkily {

bool takes_audio(const TrackInput& input) noexcept {
    return input.source == TrackInput::Source::audio ||
           input.source == TrackInput::Source::midi_and_audio;
}

std::vector<AudioInputRoute> audio_input_routes(const Song& song, std::size_t selected) {
    std::vector<AudioInputRoute> routes(song.tracks.size());
    const bool any_armed = std::any_of(song.tracks.begin(), song.tracks.end(),
                                       [](const Track& track) { return track.input.armed; });
    for (std::size_t index = 0; index < song.tracks.size(); ++index) {
        const auto& input = song.tracks[index].input;
        if (!takes_audio(input) || input.audio_channels == 0) continue;
        auto& route = routes[index];
        route.first_channel = input.audio_first_channel;
        route.channels = std::min<std::uint8_t>(input.audio_channels, 2);
        // With nothing armed, the selected track records (decision 4).
        route.capture = any_armed ? input.armed : index == selected;
        route.monitor = input.monitor == TrackInput::Monitor::on ||
                        (input.monitor == TrackInput::Monitor::automatic && input.armed);
    }
    return routes;
}

} // namespace blokkily
