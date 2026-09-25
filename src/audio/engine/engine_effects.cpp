#include "engine_effects.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace blokkily::engine {
namespace {

// A tail a plugin reports as endless (or absurdly long) is cut here, so a
// bounce always ends.
constexpr double longest_tail_seconds = 60.0;

// Gives `chain` exactly `count` slots, keeping the processors already in the
// ones that stay.
void shape_chain(InsertChain& chain, std::size_t count) {
    chain.slots.resize(count);
    for (auto& slot : chain.slots)
        if (!slot) slot = std::make_unique<EffectSlotPlayback>();
}

// Activates every processor on `chain`, sizes each slot's bypass delay, and
// returns the chain's latency. `tail` grows to the longest tail seen.
bool activate_chain(InsertChain& chain, double sample_rate, std::uint32_t maximum_block,
                    std::uint64_t& tail, std::string* error) {
    const auto longest = static_cast<std::uint64_t>(sample_rate * longest_tail_seconds);
    chain.latency = 0;
    for (auto& slot : chain.slots) {
        slot->latency = 0;
        if (slot->instance) {
            if (!slot->instance->activate(sample_rate, 1, maximum_block)) {
                if (error != nullptr) *error = "an effect refused to activate";
                return false;
            }
            slot->latency = slot->instance->latency_samples();
            tail = std::max(tail, std::min(slot->instance->tail_samples(), longest));
        }
        slot->through.prepare(slot->latency);
        chain.latency += slot->latency;
    }
    return true;
}

void shape_sends(InsertChain& chain, std::size_t returns) {
    chain.sends.resize(returns);
    for (auto& send : chain.sends)
        if (!send) send = std::make_unique<SendPlayback>();
}

void apply_bypass(InsertChain& chain, const std::vector<EffectSlot>& inserts) {
    const auto count = std::min(chain.slots.size(), inserts.size());
    for (std::size_t index = 0; index < count; ++index)
        chain.slots[index]->bypass.store(inserts[index].bypass, std::memory_order_relaxed);
}

// Adds `gain` times `from` into `into`, over the frames both hold.
void add_scaled(float* into_left, float* into_right, StereoBlock from, float gain,
                std::size_t frames) noexcept {
    for (std::size_t frame = 0; frame < frames; ++frame) {
        into_left[frame] += from.left[frame] * gain;
        into_right[frame] += from.right[frame] * gain;
    }
}

} // namespace

void StereoDelay::prepare(std::uint32_t delay) {
    samples = delay;
    left.resize(delay);
    right.resize(delay);
}

void StereoDelay::process(StereoBlock block) noexcept {
    if (samples == 0) return;
    const auto frames = std::min(block.left.size(), block.right.size());
    for (std::size_t frame = 0; frame < frames; ++frame) {
        block.left[frame] = left.process(block.left[frame], samples);
        block.right[frame] = right.process(block.right[frame], samples);
    }
}

void StereoDelay::clear() noexcept {
    left.clear();
    right.clear();
}

void reset_chain(InsertChain& chain) {
    for (auto& slot : chain.slots) {
        if (slot->instance) slot->instance->reset();
        slot->through.clear();
    }
    chain.compensation.clear();
}

void reset_buses(BusPlayback& buses) {
    for (auto& bus : buses.returns) {
        reset_chain(bus->chain);
        std::fill(bus->left.begin(), bus->left.end(), 0.0F);
        std::fill(bus->right.begin(), bus->right.end(), 0.0F);
        bus->peak.store(0.0F, std::memory_order_relaxed);
    }
    reset_chain(buses.master);
    buses.direct_compensation.clear();
    std::fill(buses.return_left.begin(), buses.return_left.end(), 0.0F);
    std::fill(buses.return_right.begin(), buses.return_right.end(), 0.0F);
}

bool prepare_effects(const Song& song, std::vector<InsertChain*> chains, BusPlayback& buses,
                     double sample_rate, std::uint32_t maximum_block, std::string* error) {
    buses.tail = 0;
    // Tracks: each chain shaped to the song, then aligned to the slowest.
    std::uint32_t track_latency = 0;
    for (std::size_t track = 0; track < chains.size(); ++track) {
        auto& chain = *chains[track];
        shape_chain(chain, track < song.tracks.size() ? song.tracks[track].inserts.size() : 0);
        if (!activate_chain(chain, sample_rate, maximum_block, buses.tail, error)) return false;
        shape_sends(chain, song.returns.size());
        track_latency = std::max(track_latency, chain.latency);
    }
    for (auto* chain : chains) chain->compensation.prepare(track_latency - chain->latency);

    // Returns: the same among themselves.
    buses.returns.resize(song.returns.size());
    std::uint32_t return_latency = 0;
    for (std::size_t index = 0; index < buses.returns.size(); ++index) {
        auto& bus = buses.returns[index];
        if (!bus) bus = std::make_unique<ReturnPlayback>();
        bus->left.assign(maximum_block, 0.0F);
        bus->right.assign(maximum_block, 0.0F);
        shape_chain(bus->chain, song.returns[index].inserts.size());
        if (!activate_chain(bus->chain, sample_rate, maximum_block, buses.tail, error))
            return false;
        shape_sends(bus->chain, 0);
        bus->peak.store(0.0F, std::memory_order_relaxed);
        return_latency = std::max(return_latency, bus->chain.latency);
    }
    for (auto& bus : buses.returns)
        bus->chain.compensation.prepare(return_latency - bus->chain.latency);
    buses.direct_compensation.prepare(return_latency);
    buses.return_left.assign(maximum_block, 0.0F);
    buses.return_right.assign(maximum_block, 0.0F);

    shape_chain(buses.master, song.master_inserts.size());
    if (!activate_chain(buses.master, sample_rate, maximum_block, buses.tail, error)) return false;
    shape_sends(buses.master, 0);
    buses.master.compensation.prepare(0);

    buses.track_latency = track_latency;
    buses.return_latency = return_latency;
    return true;
}

void apply_effect_mix(const Song& song, std::vector<InsertChain*> chains, BusPlayback& buses) {
    const bool solo = song.any_solo();
    const auto tracks = std::min(chains.size(), song.tracks.size());
    for (std::size_t index = 0; index < tracks; ++index) {
        auto& chain = *chains[index];
        const auto& track = song.tracks[index];
        const bool heard = audible(track.mix, solo);
        chain.fader.store(heard ? static_cast<float>(db_to_linear(track.mix.gain_db)) : 0.0F,
                          std::memory_order_relaxed);
        chain.audible.store(heard, std::memory_order_relaxed);
        apply_bypass(chain, track.inserts);
        // Each send's final value is worked out first and stored once, so the
        // callback never sees a send switched off on the way to a new level.
        for (std::size_t bus = 0; bus < chain.sends.size(); ++bus) {
            float level = 0.0F;
            bool pre = false;
            for (const auto& send : track.sends)
                if (send.bus == bus) {
                    level = static_cast<float>(db_to_linear(send.level_db));
                    pre = send.pre_fader;
                }
            chain.sends[bus]->pre_fader.store(pre, std::memory_order_relaxed);
            chain.sends[bus]->level.store(level, std::memory_order_relaxed);
        }
    }
    const auto returns = std::min(buses.returns.size(), song.returns.size());
    for (std::size_t index = 0; index < returns; ++index) {
        auto& bus = *buses.returns[index];
        // Solo-safe (decision 10): soloing a track never silences a return.
        const auto gain = strip_gain(song.returns[index].mix, false);
        bus.gain_left.store(gain.left, std::memory_order_relaxed);
        bus.gain_right.store(gain.right, std::memory_order_relaxed);
        apply_bypass(bus.chain, song.returns[index].inserts);
    }
    apply_bypass(buses.master, song.master_inserts);
}

void begin_buses(BusPlayback& buses, std::size_t frames) noexcept {
    for (auto& bus : buses.returns) {
        const auto count = std::min(frames, bus->left.size());
        std::fill_n(bus->left.begin(), count, 0.0F);
        std::fill_n(bus->right.begin(), count, 0.0F);
    }
    const auto count = std::min(frames, buses.return_left.size());
    std::fill_n(buses.return_left.begin(), count, 0.0F);
    std::fill_n(buses.return_right.begin(), count, 0.0F);
}

void run_insert_chain(InsertChain& chain, StereoBlock block, BusKind kind, std::uint32_t bus,
                      const TransportInfo& transport, const EditDrain& drain) noexcept {
    const auto frames = std::min(block.left.size(), block.right.size());
    for (std::size_t index = 0; index < chain.slots.size(); ++index) {
        auto& slot = *chain.slots[index];
        if (!slot.instance) continue;
        // Taken whether or not the slot is bypassed, so a lane stays where
        // the song is while its effect sits out.
        const ProcessorAddress where{kind, bus, static_cast<std::int32_t>(index)};
        const auto events = drain.events != nullptr ? drain.events(drain.context, where)
                                                    : std::span<const PluginEvent>{};
        if (slot.bypass.load(std::memory_order_relaxed)) {
            // Bypassed: the input, as late as the plugin would have made it.
            slot.through.process(block);
            continue;
        }
        if (slot.through.samples > 0)
            for (std::size_t frame = 0; frame < frames; ++frame) {
                slot.through.left.write(block.left[frame]);
                slot.through.right.write(block.right[frame]);
            }
        slot.instance->set_transport(transport);
        slot.instance->process(block, events);
        if (drain.drain != nullptr) drain.drain(drain.context, *slot.instance, where);
    }
}

void apply_track_compensation(InsertChain& chain, StereoBlock track) noexcept {
    chain.compensation.process(track);
}

void mix_sends(InsertChain& chain, BusPlayback& buses, StereoBlock track) noexcept {
    if (!chain.audible.load(std::memory_order_relaxed)) return;
    const float fader = chain.fader.load(std::memory_order_relaxed);
    const auto frames = std::min(track.left.size(), track.right.size());
    const auto count = std::min(chain.sends.size(), buses.returns.size());
    for (std::size_t index = 0; index < count; ++index) {
        const auto& send = *chain.sends[index];
        const float level = send.level.load(std::memory_order_relaxed);
        if (level == 0.0F) continue;
        const float gain =
            send.pre_fader.load(std::memory_order_relaxed) ? level : level * fader;
        if (gain == 0.0F) continue;
        auto& bus = *buses.returns[index];
        add_scaled(bus.left.data(), bus.right.data(), track, gain,
                   std::min(frames, bus.left.size()));
    }
}

void mix_sends_ramp(InsertChain& chain, BusPlayback& buses, StereoBlock track, float fader_from,
                    float fader_to, float audible_from, float audible_to) noexcept {
    const auto frames = std::min(track.left.size(), track.right.size());
    if (frames == 0) return;
    const auto count = std::min(chain.sends.size(), buses.returns.size());
    const float step = 1.0F / static_cast<float>(frames);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& send = *chain.sends[index];
        const float level = send.level.load(std::memory_order_relaxed);
        if (level == 0.0F) continue;
        const bool pre = send.pre_fader.load(std::memory_order_relaxed);
        const float from = level * (pre ? audible_from : fader_from);
        const float to = level * (pre ? audible_to : fader_to);
        if (from == 0.0F && to == 0.0F) continue;
        auto& bus = *buses.returns[index];
        const auto n = std::min(frames, bus.left.size());
        const float slope = (to - from) * step;
        for (std::size_t frame = 0; frame < n; ++frame) {
            const float gain = from + slope * static_cast<float>(frame);
            bus.left[frame] += track.left[frame] * gain;
            bus.right[frame] += track.right[frame] * gain;
        }
    }
}

void process_returns(BusPlayback& buses, std::size_t frames, const TransportInfo& transport,
                     const EditDrain& drain) noexcept {
    const StereoBlock sum{std::span(buses.return_left.data(), frames),
                          std::span(buses.return_right.data(), frames)};
    for (std::size_t index = 0; index < buses.returns.size(); ++index) {
        auto& bus = *buses.returns[index];
        if (bus.left.size() < frames) continue;
        const StereoBlock block{std::span(bus.left.data(), frames),
                                std::span(bus.right.data(), frames)};
        run_insert_chain(bus.chain, block, BusKind::ret, static_cast<std::uint32_t>(index),
                         transport, drain);
        bus.chain.compensation.process(block);
        const float peak = mix_into(sum, block.left, block.right,
                                    {bus.gain_left.load(std::memory_order_relaxed),
                                     bus.gain_right.load(std::memory_order_relaxed)});
        bus.peak.store(std::max(bus.peak.load(std::memory_order_relaxed), peak),
                       std::memory_order_relaxed);
    }
}

void apply_master_compensation(BusPlayback& buses, StereoBlock direct) noexcept {
    buses.direct_compensation.process(direct);
    if (buses.returns.empty()) return;
    const auto frames =
        std::min({direct.left.size(), direct.right.size(), buses.return_left.size()});
    for (std::size_t frame = 0; frame < frames; ++frame) {
        direct.left[frame] += buses.return_left[frame];
        direct.right[frame] += buses.return_right[frame];
    }
}

void run_master_inserts(BusPlayback& buses, StereoBlock direct, const TransportInfo& transport,
                        const EditDrain& drain) noexcept {
    run_insert_chain(buses.master, direct, BusKind::master, 0, transport, drain);
}

} // namespace blokkily::engine
