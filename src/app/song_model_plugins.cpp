#include "song_model.hpp"

// Plugin state moved by the plugin's own window (item 2.6).

bool SongModel::commitInstrumentState(int track, std::vector<std::byte> state) {
    if (!validTrack(track) || state.empty()) return false;
    auto& slot = song_.tracks[static_cast<std::size_t>(track)].instrument;
    if (slot.format.empty() || slot.state == state) return false;
    // The step holds the state from before the gesture, which undo puts back.
    checkpoint();
    slot.state = std::move(state);
    emit songChanged();
    return true;
}
