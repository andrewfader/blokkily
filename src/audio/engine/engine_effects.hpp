#pragma once

// Chunk stages 7, 8 and 10 (plan F-D), and the bus stages after every track:
// the insert chain, plugin delay compensation, sends, returns and the master
// inserts (item 2.4).
//
// Latency. Every insert reports its latency when the engine is prepared. A
// track's chain delays it by the sum of its slots; its compensation delay
// brings every track up to the slowest chain (T), so tracks reach the direct
// bus and the sends aligned. Returns do the same among themselves (R), and
// the direct bus is delayed by R so that it meets the returns. The master
// inserts add M. What the speakers hear is the song T + R + M samples late,
// which is what SongEngine::output_latency() reports and a bounce trims.
//
// Live moves. Bypass, send levels, pre/post, and the return strips are
// atomics the control thread writes and the callback reads. A bypassed slot
// passes its input through a delay of its own latency, so bypassing never
// changes the compensation and never needs a prepare.

#include "blokkily/audio/mixer.hpp"
#include "blokkily/effects/delay_line.hpp"
#include "blokkily/model/processor_address.hpp"
#include "blokkily/model/song.hpp"
#include "blokkily/plugins/plugin.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace blokkily::engine {

// A fixed delay applied in place to a stereo block. Sized on the control
// thread; process() is real-time safe.
struct StereoDelay {
    DelayLine left;
    DelayLine right;
    std::uint32_t samples = 0;

    void prepare(std::uint32_t delay);
    void process(StereoBlock block) noexcept;
    // Forgets what is in flight, keeping the size. Allocates nothing.
    void clear() noexcept;
};

// One insert slot. A slot whose plugin failed to load has no instance and
// passes audio through untouched.
struct EffectSlotPlayback {
    std::unique_ptr<PluginInstance> instance;
    std::atomic<bool> bypass{false};
    // Reported by the instance when the engine was prepared.
    std::uint32_t latency = 0;
    // The slot's input, delayed by its latency: what a bypassed slot plays,
    // kept current while it is not bypassed so switching is seamless.
    StereoDelay through;
};

// A track's send to one return. Level 0 means the track does not send there.
struct SendPlayback {
    std::atomic<float> level{0.0F};
    std::atomic<bool> pre_fader{false};
};

// The insert slots after the instrument (or on a return, or the master), the
// compensation that aligns this bus with its neighbours, and a track's sends.
struct InsertChain {
    std::vector<std::unique_ptr<EffectSlotPlayback>> slots;
    // Sum of the slots' latencies, as of prepare().
    std::uint32_t latency = 0;
    StereoDelay compensation;
    // One per return bus, preallocated, so adding a send is a level change.
    std::vector<std::unique_ptr<SendPlayback>> sends;
    // The strip gain without pan, zero when the strip is inaudible: what a
    // post-fader send takes (decision 10: post-fader, pre-pan).
    std::atomic<float> fader{1.0F};
    std::atomic<bool> audible{true};
};

// One return bus: the sum of the sends into it, its chain and its strip.
struct ReturnPlayback {
    std::vector<float> left;
    std::vector<float> right;
    InsertChain chain;
    std::atomic<float> gain_left{1.0F};
    std::atomic<float> gain_right{1.0F};
    std::atomic<float> peak{0.0F};
};

// The return buses and the master chain, shared by every track.
struct BusPlayback {
    std::vector<std::unique_ptr<ReturnPlayback>> returns;
    InsertChain master;
    // Delays the direct bus by the slowest return, so the two meet aligned.
    StereoDelay direct_compensation;
    // Every return's output, summed, before it meets the direct bus.
    std::vector<float> return_left;
    std::vector<float> return_right;
    std::uint32_t track_latency = 0;   // T
    std::uint32_t return_latency = 0;  // R
    // The longest tail any insert reported, in samples.
    std::uint64_t tail = 0;
};

// How a chain hands each slot's parameter edits to the engine's ring.
struct EditDrain {
    void (*drain)(void* context, PluginInstance& processor, ProcessorAddress where) noexcept =
        nullptr;
    void* context = nullptr;
};

// Control thread, after the processors are in place: shapes every chain to
// the song, activates each insert, reads its latency and tail, and sizes the
// compensation and bypass delays. `chains` holds each track's chain in track
// order. False, with `error` set, when an insert refuses to activate.
[[nodiscard]] bool prepare_effects(const Song& song, std::vector<InsertChain*> chains,
                                   BusPlayback& buses, double sample_rate,
                                   std::uint32_t maximum_block, std::string* error);

// Control thread, any time: the live values of the song's inserts, sends and
// return strips. Only atomics are written; a song whose shape no longer
// matches the prepared graph is applied as far as it matches.
void apply_effect_mix(const Song& song, std::vector<InsertChain*> chains, BusPlayback& buses);

// Before any track renders: the return buses start the chunk silent.
void begin_buses(BusPlayback& buses, std::size_t frames) noexcept;
// 7. Runs the insert chain in place and drains each slot's parameter edits.
void run_insert_chain(InsertChain& chain, StereoBlock block, BusKind kind, std::uint32_t bus,
                      const TransportInfo& transport, const EditDrain& drain) noexcept;
// 8. Delays the track so that every path reaches the bus aligned.
void apply_track_compensation(InsertChain& chain, StereoBlock track) noexcept;
// 10. Mixes the track into the returns it sends to: pre-fader sends take the
// signal after the chain, post-fader sends that times the fader (not the
// pan). An inaudible track sends nothing.
void mix_sends(InsertChain& chain, BusPlayback& buses, StereoBlock track) noexcept;
// After every track: each return's chain, compensation and strip, summed.
void process_returns(BusPlayback& buses, std::size_t frames, const TransportInfo& transport,
                     const EditDrain& drain) noexcept;
// The direct bus delayed to meet the returns, then the returns added.
void apply_master_compensation(BusPlayback& buses, StereoBlock direct) noexcept;
void run_master_inserts(BusPlayback& buses, StereoBlock direct, const TransportInfo& transport,
                        const EditDrain& drain) noexcept;

// While the render callback is not running (SongEngine::reset_processing):
// every processor on the chain is reset() and every delay the chain keeps
// (bypass lines, compensation) is emptied, so it holds no signal.
void reset_chain(InsertChain& chain);
// The same for every return's chain and the direct bus's compensation.
void reset_buses(BusPlayback& buses);

} // namespace blokkily::engine
