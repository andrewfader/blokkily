#include "engine_effects.hpp"

namespace blokkily::engine {

void run_insert_chain(InsertChain&, StereoBlock) noexcept {}
void apply_track_compensation(InsertChain&, StereoBlock) noexcept {}
void mix_sends(InsertChain&, BusPlayback&, StereoBlock, StripGain) noexcept {}
void process_returns(BusPlayback&, StereoBlock) noexcept {}
void apply_master_compensation(BusPlayback&, StereoBlock) noexcept {}
void run_master_inserts(BusPlayback&, StereoBlock) noexcept {}

} // namespace blokkily::engine
