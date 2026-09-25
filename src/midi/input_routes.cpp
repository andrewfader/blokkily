#include "blokkily/midi/input_routes.hpp"

namespace blokkily {

bool armed_for_notes(const Song& song, std::size_t track) noexcept {
    if (track >= song.tracks.size() || track >= routable_tracks) return false;
    const auto& input = song.tracks[track].input;
    return input.armed && (input.source == TrackInput::Source::midi ||
                           input.source == TrackInput::Source::midi_and_audio);
}

TrackMask surface_routes(const Song& song, std::size_t selected_track) noexcept {
    TrackMask armed = 0;
    for (std::size_t track = 0; track < song.tracks.size(); ++track)
        if (armed_for_notes(song, track)) armed |= track_bit(track);
    if (armed != 0) return armed;
    return selected_track < song.tracks.size() ? track_bit(selected_track) : TrackMask{0};
}

ChannelRoutes midi_routes(const Song& song, std::size_t selected_track) noexcept {
    ChannelRoutes routes{};
    bool any_armed = false;
    for (std::size_t track = 0; track < song.tracks.size(); ++track) {
        if (!armed_for_notes(song, track)) continue;
        any_armed = true;
        const auto channel = song.tracks[track].input.midi_channel;
        for (std::size_t c = 0; c < routes.size(); ++c)
            if (channel < 0 || static_cast<std::size_t>(channel) == c)
                routes[c] |= track_bit(track);
    }
    // Nothing armed: the keyboard plays the selected track on every channel,
    // which is what it did before tracks could be armed (decision 4).
    if (!any_armed)
        routes.fill(selected_track < song.tracks.size() ? track_bit(selected_track)
                                                        : TrackMask{0});
    return routes;
}

} // namespace blokkily
