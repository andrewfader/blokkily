#pragma once

// The effects Blokkily provides itself (plan item 1.8): a three-band EQ, a
// delay, a reverb and a compressor. Each is a PluginInstance like any plugin:
// it processes the block IN PLACE (the track signal in, the effect out),
// takes parameter_value and parameter_modulation events at their sample
// offsets, reports its tail, and saves and loads its parameters as state. The
// engine and the browser meet them only through this header and the
// PluginInstance interface; registering them with the processor factory and
// putting them in an insert chain is item 2.4.
//
// Parameter ids are the indices below; values are in the units the names
// say (decibels, hertz, milliseconds, beats, or 0..1).

#include "blokkily/plugins/plugin.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace blokkily {

// The InstrumentSlot::format every built-in effect is stored under; the
// slot's identifier names which one.
inline constexpr std::string_view builtin_effect_format = "Built-in";

struct BuiltinEffectInfo {
    std::string identifier; // "eq3", "delay", "reverb", "compressor"
    std::string name;       // what a browser shows
};

// Every built-in effect, in the order a browser lists them.
[[nodiscard]] std::vector<BuiltinEffectInfo> builtin_effects();

// A new, inactive instance of the effect `identifier` names, at its default
// parameters, or nullptr for an unknown identifier. Control thread.
[[nodiscard]] std::unique_ptr<PluginInstance> create_builtin_effect(std::string_view identifier);

// Low shelf, peaking mid band and high shelf (RBJ biquads).
namespace eq3 {
enum Parameter : std::int32_t {
    low_gain_db = 0,  // -24..24, 0
    low_hz = 1,       // 20..1000, 200
    mid_gain_db = 2,  // -24..24, 0
    mid_hz = 3,       // 100..10000, 1000
    mid_q = 4,        // 0.1..10, 0.707
    high_gain_db = 5, // -24..24, 0
    high_hz = 6,      // 1000..20000, 5000
};
}

// A feedback echo, free-running in milliseconds or synced to the song's
// tempo (from set_transport) in beats. out = (1 - mix) * in + mix * echo.
namespace delay {
enum Parameter : std::int32_t {
    time_ms = 0,  // 1..2000, 250
    feedback = 1, // 0..0.95, 0.5: each echo is this fraction of the one before
    mix = 2,      // 0..1, 0.5
    sync = 3,     // 0 or 1 (>= 0.5 means synced), 0
    beats = 4,    // 0.0625..4, 0.5: the echo time while synced
};
}

// A Schroeder-Moorer (Freeverb) stereo reverb.
namespace reverb {
enum Parameter : std::int32_t {
    size = 0,    // 0..1, 0.7
    damping = 1, // 0..1, 0.5
    width = 2,   // 0..1, 1
    mix = 3,     // 0..1, 0.3
};
}

// A stereo-linked peak compressor with a hard knee. Above the threshold the
// level rises 1/ratio dB per dB.
namespace compressor {
enum Parameter : std::int32_t {
    threshold_db = 0, // -60..0, -20
    ratio = 1,        // 1..20, 4
    attack_ms = 2,    // 0.1..200, 10
    release_ms = 3,   // 5..2000, 100
    makeup_db = 4,    // 0..24, 0
};
}

} // namespace blokkily
