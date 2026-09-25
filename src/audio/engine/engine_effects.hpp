#pragma once

// Chunk stages 7, 8 and 10 (plan F-D), and the bus stages after every track:
// the insert chain, plugin delay compensation, sends, returns and the master
// inserts. Item 2.4 fills these in; until then every stage is a no-op.

#include "blokkily/audio/mixer.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <cstdint>

namespace blokkily::engine {

// Per track: the insert slots after the instrument, and its compensation
// delay line.
struct InsertChain {};

// The return buses and the master chain, shared by every track.
struct BusPlayback {};

// 7. Runs the insert chain in place and drains each slot's parameter edits.
void run_insert_chain(InsertChain& chain, StereoBlock track) noexcept;
// 8. Delays the track so that every path reaches the bus aligned.
void apply_track_compensation(InsertChain& chain, StereoBlock track) noexcept;
// 10. Mixes the track into the returns it sends to: `pre_fader` is the signal
// after the chain and before the strip gain; `post_fader` after gain and pan.
void mix_sends(InsertChain& chain, BusPlayback& buses, StereoBlock pre_fader,
               StripGain gain) noexcept;
// After every track: the returns summed into the direct bus, then the master
// compensation, then the master inserts. The master gain follows.
void process_returns(BusPlayback& buses, StereoBlock direct) noexcept;
void apply_master_compensation(BusPlayback& buses, StereoBlock direct) noexcept;
void run_master_inserts(BusPlayback& buses, StereoBlock direct) noexcept;

} // namespace blokkily::engine
