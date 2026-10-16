// Pattern swing: how far even sixteenths of the open pattern are delayed
// when they play. Every part of the section takes the same value, like
// length. One step of history; the engine is rebuilt so playback hears it.

#include "song_model.hpp"

#include <algorithm>

int SongModel::patternSwing() const {
    return editPattern().swing();
}

bool SongModel::setPatternSwing(int percent) {
    const int amount = std::clamp(percent, 0, 100);
    bool dirty = false;
    for (const auto& slot : song_.patterns) {
        if (slot.section != static_cast<std::size_t>(current_section_)) continue;
        if (slot.pattern.swing() != amount) dirty = true;
    }
    if (!dirty) return true;
    checkpoint();
    for (auto& slot : song_.patterns) {
        if (slot.section != static_cast<std::size_t>(current_section_)) continue;
        slot.pattern.set_swing(amount);
    }
    notifyStructureChanged();
    return true;
}
