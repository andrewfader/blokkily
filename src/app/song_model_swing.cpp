// Pattern swing: how far even sixteenths of the open pattern are delayed
// when they play. Every part of the section takes the same value, like
// length, unless one track's part is given its own (setPartSwing). One step
// of history; the engine is rebuilt so playback hears it.

#include "song_model.hpp"

#include <algorithm>
#include <optional>

int SongModel::patternSwing() const {
    return editPattern().swing();
}

bool SongModel::swingVaries() const {
    std::optional<int> seen;
    for (const auto& slot : song_.patterns) {
        if (slot.section != static_cast<std::size_t>(current_section_)) continue;
        if (seen && *seen != slot.pattern.swing()) return true;
        seen = slot.pattern.swing();
    }
    return false;
}

bool SongModel::setPartSwing(int percent) {
    const int amount = std::clamp(percent, 0, 100);
    if (editPattern().swing() == amount) return true;
    checkpoint();
    editPattern().set_swing(amount);
    notifyStructureChanged();
    return true;
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
