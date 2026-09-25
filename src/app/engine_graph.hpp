#pragma once

// The rebuild-or-go-live decision (plan F-D, C11). A song's graph signature
// names which processor sits at every address; an edit that leaves it alone is
// a recompile of the running engine, and one that changes it is a rebuild. A
// rebuild adopts the instances whose identity is unchanged, so a synth a
// producer has dialled in is the same instance afterwards, and creates only
// what is new. Control thread only; needs no Qt.

#include "processor_factory.hpp"

#include "blokkily/audio/song_engine.hpp"
#include "blokkily/model/processor_address.hpp"
#include "blokkily/model/song.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace blokkily {

// What makes a processor the same processor: the same file loaded through the
// same format under the same identifier. Its state moves as it is played and
// is not part of its identity.
struct ProcessorIdentity {
    std::string format;
    std::string path;
    std::string identifier;

    [[nodiscard]] bool empty() const noexcept { return format.empty(); }
    friend bool operator==(const ProcessorIdentity&, const ProcessorIdentity&) = default;
};
[[nodiscard]] ProcessorIdentity identity_of(const InstrumentSlot& slot);

struct GraphSignature {
    // Every address the song defines, in graph order, with what sits there
    // (an empty identity where nothing does).
    std::vector<std::pair<ProcessorAddress, ProcessorIdentity>> processors;
    // How many return buses the graph has (plan F-E adds them).
    std::size_t returns = 0;

    [[nodiscard]] const ProcessorIdentity* at(ProcessorAddress where) const;
    friend bool operator==(const GraphSignature&, const GraphSignature&) = default;
};
[[nodiscard]] GraphSignature graph_signature(const Song& song);

// Where each old track went: entry i is the new index of old track i, or empty
// when it was removed. Without one, tracks are taken to keep their indices.
using TrackRemap = std::vector<std::optional<std::size_t>>;

// The released processors the wanted graph still has a place for, each moved
// to the address it has there. Considered in order, address by address; one
// whose identity changed, or whose track was removed, is left out and is
// destroyed with `released` on the calling (control) thread.
[[nodiscard]] std::vector<ReleasedProcessor> adopt_processors(
    std::vector<ReleasedProcessor> released, const GraphSignature& built,
    const GraphSignature& wanted, const TrackRemap* remap);

struct GraphBuild {
    int loaded = 0;    // processors now in the engine
    int adopted = 0;   // of which carried over from the previous engine
    std::string error; // why the first processor that failed to load failed
    // Processors created fresh, whose saved state still has to be loaded once
    // the engine is prepared.
    std::vector<ProcessorAddress> fresh;
};

// Puts every processor `song` names into `engine`: the adopted instance where
// one is given for that address, a new one from the factory otherwise.
[[nodiscard]] GraphBuild populate_graph(SongEngine& engine, const Song& song,
                                        std::vector<ReleasedProcessor> adopted,
                                        const ProcessorContext& context);

// After prepare(): loads the song's saved state into the processors that were
// created fresh. Adopted processors keep the state they already have.
void load_fresh_state(SongEngine& engine, const Song& song, const GraphBuild& build);

} // namespace blokkily
