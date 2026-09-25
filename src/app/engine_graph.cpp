#include "engine_graph.hpp"

#include <algorithm>
#include <type_traits>

namespace blokkily {
namespace {

// The insert chain a song keeps for a bus, or nullptr.
template <typename SongType>
auto* chain_of(SongType& song, BusKind kind, std::uint32_t bus) {
    using Chain = std::conditional_t<std::is_const_v<SongType>, const std::vector<EffectSlot>,
                                     std::vector<EffectSlot>>;
    Chain* chain = nullptr;
    switch (kind) {
    case BusKind::track:
        if (bus < song.tracks.size()) chain = &song.tracks[bus].inserts;
        break;
    case BusKind::ret:
        if (bus < song.returns.size()) chain = &song.returns[bus].inserts;
        break;
    case BusKind::master:
        if (bus == 0) chain = &song.master_inserts;
        break;
    }
    return chain;
}

template <typename SongType>
auto* slot_in(SongType& song, ProcessorAddress where) {
    using Slot = std::conditional_t<std::is_const_v<SongType>, const InstrumentSlot,
                                    InstrumentSlot>;
    if (where.instrument()) {
        if (where.kind != BusKind::track || where.bus >= song.tracks.size())
            return static_cast<Slot*>(nullptr);
        return static_cast<Slot*>(&song.tracks[where.bus].instrument);
    }
    auto* chain = chain_of(song, where.kind, where.bus);
    if (chain == nullptr || static_cast<std::size_t>(where.slot) >= chain->size())
        return static_cast<Slot*>(nullptr);
    return static_cast<Slot*>(&(*chain)[static_cast<std::size_t>(where.slot)].plugin);
}

void list_chain(GraphSignature& signature, const std::vector<EffectSlot>& inserts, BusKind kind,
                std::uint32_t bus) {
    for (std::size_t slot = 0; slot < inserts.size(); ++slot)
        signature.processors.emplace_back(
            ProcessorAddress{kind, bus, static_cast<std::int32_t>(slot)},
            identity_of(inserts[slot].plugin));
}

} // namespace

ProcessorIdentity identity_of(const InstrumentSlot& slot) {
    return {slot.format, slot.path, slot.identifier};
}

const InstrumentSlot* song_slot(const Song& song, ProcessorAddress where) {
    return slot_in(song, where);
}

InstrumentSlot* song_slot(Song& song, ProcessorAddress where) { return slot_in(song, where); }

const ProcessorIdentity* GraphSignature::at(ProcessorAddress where) const {
    for (const auto& [address, identity] : processors)
        if (address == where) return &identity;
    return nullptr;
}

GraphSignature graph_signature(const Song& song) {
    GraphSignature signature;
    signature.processors.reserve(song.tracks.size());
    for (std::size_t track = 0; track < song.tracks.size(); ++track) {
        const auto bus = static_cast<std::uint32_t>(track);
        signature.processors.emplace_back(track_instrument(bus),
                                          identity_of(song.tracks[track].instrument));
        list_chain(signature, song.tracks[track].inserts, BusKind::track, bus);
    }
    for (std::size_t index = 0; index < song.returns.size(); ++index)
        list_chain(signature, song.returns[index].inserts, BusKind::ret,
                   static_cast<std::uint32_t>(index));
    list_chain(signature, song.master_inserts, BusKind::master, 0);
    signature.returns = song.returns.size();
    return signature;
}

std::vector<ReleasedProcessor> adopt_processors(std::vector<ReleasedProcessor> released,
                                                const GraphSignature& built,
                                                const GraphSignature& wanted,
                                                const TrackRemap* remap) {
    std::vector<ReleasedProcessor> adopted;
    const auto taken = [&adopted](ProcessorAddress target) {
        return std::any_of(adopted.begin(), adopted.end(),
                           [&](const ReleasedProcessor& kept) { return kept.where == target; });
    };
    // Per bus, the last insert slot a processor was adopted into, so a chain
    // keeps its order: an insert removed or added in front of the others
    // shifts them, and each follows its own identity to its new slot.
    struct Cursor {
        BusKind kind;
        std::uint32_t bus;
        std::int32_t last;
    };
    std::vector<Cursor> cursors;
    const auto cursor_for = [&cursors](BusKind kind, std::uint32_t bus) -> std::int32_t& {
        for (auto& cursor : cursors)
            if (cursor.kind == kind && cursor.bus == bus) return cursor.last;
        cursors.push_back({kind, bus, -1});
        return cursors.back().last;
    };

    for (auto& candidate : released) {
        if (!candidate.instance) continue;
        const auto* was = built.at(candidate.where);
        if (was == nullptr || was->empty()) continue;
        auto target = candidate.where;
        if (target.kind == BusKind::track && remap != nullptr) {
            if (target.bus >= remap->size() || !(*remap)[target.bus]) continue;
            target.bus = static_cast<std::uint32_t>(*(*remap)[target.bus]);
        }
        if (target.instrument()) {
            const auto* now = wanted.at(target);
            if (now == nullptr || !(*now == *was) || taken(target)) continue;
            adopted.push_back({target, std::move(candidate.instance)});
            continue;
        }
        // An insert: the first slot after the last one adopted on its bus
        // that holds the same plugin.
        auto& last = cursor_for(target.kind, target.bus);
        for (const auto& [address, identity] : wanted.processors) {
            if (address.kind != target.kind || address.bus != target.bus ||
                address.instrument() || address.slot <= last || !(identity == *was) ||
                taken(address))
                continue;
            last = address.slot;
            adopted.push_back({address, std::move(candidate.instance)});
            break;
        }
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
        const auto* slot = song_slot(song, address);
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
        const auto* slot = song_slot(song, address);
        auto* instance = engine.processor(address);
        if (slot != nullptr && instance != nullptr && !slot->state.empty())
            (void)instance->load_state(slot->state);
    }
}

int capture_processor_states(const SongEngine& engine, Song& song,
                             const GraphSignature& built) {
    int captured = 0;
    const auto wanted = graph_signature(song);
    for (const auto& [address, identity] : built.processors) {
        if (identity.empty()) continue;
        // Only where the song still names the same plugin: a slot swapped for
        // another keeps the state that belongs to the new one.
        const auto* now = wanted.at(address);
        if (now == nullptr || !(*now == identity)) continue;
        auto* instance = engine.processor(address);
        auto* slot = song_slot(song, address);
        if (instance == nullptr || slot == nullptr) continue;
        auto state = instance->save_state();
        if (state.empty()) continue;
        slot->state = std::move(state);
        ++captured;
    }
    return captured;
}

void serve_processors(const SongEngine& engine) {
    for (const auto& where : engine.processor_addresses())
        if (auto* instance = engine.processor(where)) instance->idle();
}

} // namespace blokkily
