#pragma once

#include "blokkily/model/processor_address.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace blokkily {

// An inter-track sidechain routing connection:
// Taps audio from source_track (pre-fader) and delivers it as sidechain
// key input to target processor.
struct SidechainRoute {
    std::size_t source_track = 0;
    ProcessorAddress target{};

    friend bool operator==(const SidechainRoute&, const SidechainRoute&) = default;
};

// An inter-track multi-output auxiliary routing connection:
// Routes an auxiliary output bus of source_track into dest_track.
struct MultiOutputRoute {
    std::size_t source_track = 0;
    std::uint32_t aux_bus = 1;
    std::size_t dest_track = 0;
    float gain = 1.0F;

    friend bool operator==(const MultiOutputRoute&, const MultiOutputRoute&) = default;
};

// Computes a valid topological render order for tracks so that any track acting as a
// sidechain source or multi-output source is rendered before its consumers.
// If cyclic dependencies are detected, the cycle is safely broken deterministically.
[[nodiscard]] std::vector<std::size_t> compute_track_render_order(
    std::size_t track_count,
    std::span<const SidechainRoute> sidechains,
    std::span<const MultiOutputRoute> multi_outs);

} // namespace blokkily
