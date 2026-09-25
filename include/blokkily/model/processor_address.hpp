#pragma once

#include <cstdint>

namespace blokkily {

// Where a processor sits in the song's signal graph (plan F-D). The model owns
// the type because automation targets name processors too (F-E), and the
// engine, the factory and the editor windows all address them the same way.
enum class BusKind : std::uint8_t { track, ret, master };

struct ProcessorAddress {
    BusKind kind = BusKind::track;
    std::uint32_t bus = 0;   // track or return index; 0 for the master bus
    std::int32_t slot = -1;  // -1 is the bus's instrument; 0.. are insert slots

    [[nodiscard]] bool instrument() const noexcept { return slot < 0; }
    friend bool operator==(const ProcessorAddress&, const ProcessorAddress&) = default;
};

// A track's instrument, the address every current track processor has.
[[nodiscard]] constexpr ProcessorAddress track_instrument(std::uint32_t track) noexcept {
    return {BusKind::track, track, -1};
}

} // namespace blokkily
