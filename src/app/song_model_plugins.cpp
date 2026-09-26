#include "song_model.hpp"
#include "engine_graph.hpp"

bool SongModel::commitInstrumentState(int track, std::vector<std::byte> state) {
    if (!validTrack(track)) return false;
    return commitProcessorState(blokkily::track_instrument(static_cast<std::uint32_t>(track)),
                                std::move(state));
}

bool SongModel::commitProcessorState(blokkily::ProcessorAddress where,
                                     std::vector<std::byte> state) {
    auto* slot = blokkily::song_slot(song_, where);
    if (slot == nullptr || slot->format.empty() || state.empty() || slot->state == state) return false;
    if (capturing_) checkpointTake(); else checkpoint();
    slot->state = std::move(state);
    emit songChanged();
    return true;
}

void SongModel::commitSongEdit(blokkily::Song song) {
    checkpoint();
    song_ = std::move(song);
    emit processorStatesRestored();
    notifyStructureChanged();
}

int SongModel::addParameterLane(int owner, blokkily::ProcessorAddress where, int parameter,
                                 double value) {
    if (!validTrack(owner) || blokkily::song_slot(song_, where) == nullptr) return -1;
    auto& lanes = song_.tracks[static_cast<std::size_t>(owner)].automation;
    for (std::size_t i = 0; i < lanes.size(); ++i)
        if (lanes[i].target.kind == blokkily::AutomationTarget::Kind::parameter &&
            lanes[i].target.processor == where && lanes[i].target.parameter_index == parameter)
            return static_cast<int>(i);
    checkpoint();
    lanes.push_back({{blokkily::AutomationTarget::Kind::parameter, parameter, {}, where}, {{0, value}}});
    selected_lane_ = static_cast<int>(lanes.size()) - 1;
    notifyStructureChanged();
    return selected_lane_;
}
