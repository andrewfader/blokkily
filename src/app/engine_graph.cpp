#include "engine_graph.hpp"

#include <algorithm>

namespace blokkily {
namespace {

// The slot a song keeps for an address, or nullptr. Only track instruments
// exist until the insert chains do.
const InstrumentSlot* slot_at(const Song& song, ProcessorAddress where) {
    if (where.kind != BusKind::track || !where.instrument() || where.bus >= song.tracks.size())
        return nullptr;
    return &song.tracks[where.bus].instrument;
}

} // namespace

ProcessorIdentity identity_of(const InstrumentSlot& slot) {
    return {slot.format, slot.path, slot.identifier};
}

const ProcessorIdentity* GraphSignature::at(ProcessorAddress where) const {
    for (const auto& [address, identity] : processors)
        if (address == where) return &identity;
    return nullptr;
}

GraphSignature graph_signature(const Song& song) {
    GraphSignature signature;
    signature.processors.reserve(song.tracks.size());
    for (std::size_t track = 0; track < song.tracks.size(); ++track)
        signature.processors.emplace_back(track_instrument(static_cast<std::uint32_t>(track)),
                                          identity_of(song.tracks[track].instrument));
    signature.returns = song.returns.size();
    return signature;
}

std::vector<ReleasedProcessor> adopt_processors(std::vector<ReleasedProcessor> released,
                                                const GraphSignature& built,
                                                const GraphSignature& wanted,
                                                const TrackRemap* remap) {
    std::vector<ReleasedProcessor> adopted;
    for (auto& candidate : released) {
        if (!candidate.instance) continue;
        const auto* was = built.at(candidate.where);
        if (was == nullptr || was->empty()) continue;
        auto target = candidate.where;
        if (target.kind == BusKind::track && remap != nullptr) {
            if (target.bus >= remap->size() || !(*remap)[target.bus]) continue;
            target.bus = static_cast<std::uint32_t>(*(*remap)[target.bus]);
        }
        const auto* now = wanted.at(target);
        if (now == nullptr || !(*now == *was)) continue;
        // Two processors may not claim one address.
        const bool taken = std::any_of(adopted.begin(), adopted.end(),
                                       [&](const ReleasedProcessor& kept) {
                                           return kept.where == target;
                                       });
        if (taken) continue;
        adopted.push_back({target, std::move(candidate.instance)});
    }
    return adopted;
}

GraphBuild populate_graph(SongEngine& engine, const Song& song,
                          std::vector<ReleasedProcessor> adopted,
                          const ProcessorContext& context) {
    GraphBuild build;
    for (const auto& [address, identity] : graph_signature(song).processors) {
        if (identity.empty()) continue;
        const auto kept = std::find_if(adopted.begin(), adopted.end(),
                                       [&](const ReleasedProcessor& candidate) {
                                           return candidate.where == address && candidate.instance;
                                       });
        if (kept != adopted.end()) {
            engine.set_processor(address, std::move(kept->instance));
            ++build.adopted;
            ++build.loaded;
            continue;
        }
        const auto* slot = slot_at(song, address);
        if (slot == nullptr) continue;
        std::string reason;
        auto instance = create_processor(*slot, context, &reason);
        if (!instance) {
            if (build.error.empty()) build.error = reason;
            continue;
        }
        engine.set_processor(address, std::move(instance));
        build.fresh.push_back(address);
        ++build.loaded;
    }
    return build;
}

void load_fresh_state(SongEngine& engine, const Song& song, const GraphBuild& build) {
    for (const auto& address : build.fresh) {
        const auto* slot = slot_at(song, address);
        auto* instance = engine.processor(address);
        if (slot != nullptr && instance != nullptr && !slot->state.empty())
            (void)instance->load_state(slot->state);
    }
}

} // namespace blokkily
