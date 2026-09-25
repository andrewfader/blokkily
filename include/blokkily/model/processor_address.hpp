#pragma once

#include <cstdint>

namespace blokkily {

// Where a processor sits in the mixer (plan §F-D). The model uses it to say
// which processor an automation lane drives; the engine uses the same address
// to set, find, adopt and release processor instances. A track's instrument is
// slot -1; its insert effects are slots 0, 1, 2... in chain order. A return
// bus has no instrument, so its slots start at 0; the master bus is bus 0 of
// kind master.
enum class BusKind : std::uint8_t { track, ret, master };

struct ProcessorAddress {
    BusKind kind = BusKind::track;
    std::uint32_t bus = 0;
    std::int32_t slot = -1; // -1 = instrument

    friend bool operator==(const ProcessorAddress&, const ProcessorAddress&) = default;
};

} // namespace blokkily
