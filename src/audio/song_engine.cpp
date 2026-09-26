#include "blokkily/audio/song_engine.hpp"

#include "engine/arrangement.hpp"
#include "engine/engine_automation.hpp"
#include "engine/engine_clips.hpp"
#include "engine/engine_effects.hpp"
#include "engine/engine_input.hpp"
#include "engine/engine_metronome.hpp"
#include "engine/track_playback.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace blokkily {
namespace {
// An engine that has never been prepared still has to render silence rather
// than reach through a null arrangement.
const std::vector<TimedPluginEvent>& empty_timeline() noexcept {
    static const std::vector<TimedPluginEvent> nothing;
    return nothing;
}
// The same for the per-arrangement stage data. Built before main(), so the
// callback never runs its construction.
const engine::Arrangement nothing_arranged{};

// The arrangement handoff word (SongEngine::handoff_): three four-bit fields
// naming a slot each, `no_slot` where a field names none.
constexpr std::uint32_t no_slot = 0xFU;
constexpr unsigned queued_shift = 0;
constexpr unsigned rendering_shift = 4;
constexpr unsigned retiring_shift = 8;
constexpr std::uint32_t field(std::uint32_t state, unsigned shift) noexcept {
    return (state >> shift) & no_slot;
}
constexpr std::uint32_t with_field(std::uint32_t state, unsigned shift,
                                   std::uint32_t slot) noexcept {
    return (state & ~(no_slot << shift)) | ((slot & no_slot) << shift);
}
constexpr std::uint32_t handoff_state(std::uint32_t queued, std::uint32_t rendering,
                                      std::uint32_t retiring) noexcept {
    return (queued << queued_shift) | (rendering << rendering_shift) |
           (retiring << retiring_shift);
}
} // namespace

SongEngine::SongEngine()
    : buses_(std::make_unique<engine::BusPlayback>()),
      metronome_(std::make_unique<engine::MetronomePlayback>()) {}
SongEngine::~SongEngine() = default;

std::unique_ptr<PluginInstance>* SongEngine::processor_slot(ProcessorAddress where) const {
    engine::InsertChain* chain = nullptr;
    switch (where.kind) {
    case BusKind::track:
        if (where.bus >= tracks_.size()) return nullptr;
        if (where.instrument()) return &tracks_[where.bus]->instrument;
        chain = &tracks_[where.bus]->chain;
        break;
    case BusKind::ret:
        if (where.instrument() || where.bus >= buses_->returns.size()) return nullptr;
        chain = &buses_->returns[where.bus]->chain;
        break;
    case BusKind::master:
        if (where.instrument() || where.bus != 0) return nullptr;
        chain = &buses_->master;
        break;
    }
    if (chain == nullptr || static_cast<std::size_t>(where.slot) >= chain->slots.size())
        return nullptr;
    return &chain->slots[static_cast<std::size_t>(where.slot)]->instance;
}

void SongEngine::set_processor(ProcessorAddress where, std::unique_ptr<PluginInstance> instance) {
    // Makes room for the address first; prepare() trims every chain to the
    // song afterwards.
    engine::InsertChain* chain = nullptr;
    switch (where.kind) {
    case BusKind::track:
        while (tracks_.size() <= where.bus) tracks_.push_back(std::make_unique<TrackPlayback>());
        if (!where.instrument()) chain = &tracks_[where.bus]->chain;
        break;
    case BusKind::ret:
        if (where.instrument()) return;
        while (buses_->returns.size() <= where.bus)
            buses_->returns.push_back(std::make_unique<engine::ReturnPlayback>());
        chain = &buses_->returns[where.bus]->chain;
        break;
    case BusKind::master:
        if (where.instrument() || where.bus != 0) return;
        chain = &buses_->master;
        break;
    }
    if (chain != nullptr)
        while (chain->slots.size() <= static_cast<std::size_t>(where.slot))
            chain->slots.push_back(std::make_unique<engine::EffectSlotPlayback>());
    if (auto* slot = processor_slot(where)) *slot = std::move(instance);
}

PluginInstance* SongEngine::processor(ProcessorAddress where) const {
    const auto* slot = processor_slot(where);
    return slot == nullptr ? nullptr : slot->get();
}

std::vector<ReleasedProcessor> SongEngine::release_processors() {
    std::vector<ReleasedProcessor> released;
    const auto release_chain = [&released](engine::InsertChain& chain, BusKind kind,
                                           std::uint32_t bus) {
        for (std::size_t slot = 0; slot < chain.slots.size(); ++slot)
            if (chain.slots[slot]->instance)
                released.push_back({{kind, bus, static_cast<std::int32_t>(slot)},
                                    std::move(chain.slots[slot]->instance)});
    };
    for (std::size_t index = 0; index < tracks_.size(); ++index) {
        auto& instrument = tracks_[index]->instrument;
        if (instrument)
            released.push_back({track_instrument(static_cast<std::uint32_t>(index)),
                                std::move(instrument)});
        release_chain(tracks_[index]->chain, BusKind::track, static_cast<std::uint32_t>(index));
    }
    for (std::size_t index = 0; index < buses_->returns.size(); ++index)
        release_chain(buses_->returns[index]->chain, BusKind::ret,
                      static_cast<std::uint32_t>(index));
    release_chain(buses_->master, BusKind::master, 0);
    return released;
}

std::vector<engine::InsertChain*> SongEngine::track_chains() const {
    std::vector<engine::InsertChain*> chains;
    chains.reserve(tracks_.size());
    for (const auto& track : tracks_) chains.push_back(&track->chain);
    return chains;
}

std::vector<ProcessorAddress> SongEngine::processor_addresses() const {
    std::vector<ProcessorAddress> addresses;
    const auto chain_addresses = [&addresses](const engine::InsertChain& chain, BusKind kind,
                                              std::uint32_t bus) {
        for (std::size_t slot = 0; slot < chain.slots.size(); ++slot)
            if (chain.slots[slot]->instance)
                addresses.push_back({kind, bus, static_cast<std::int32_t>(slot)});
    };
    for (std::size_t index = 0; index < tracks_.size(); ++index) {
        const auto bus = static_cast<std::uint32_t>(index);
        if (tracks_[index]->instrument) addresses.push_back(track_instrument(bus));
        chain_addresses(tracks_[index]->chain, BusKind::track, bus);
    }
    for (std::size_t index = 0; index < buses_->returns.size(); ++index)
        chain_addresses(buses_->returns[index]->chain, BusKind::ret,
                        static_cast<std::uint32_t>(index));
    chain_addresses(buses_->master, BusKind::master, 0);
    return addresses;
}

void SongEngine::reset_processing() {
    for (auto& track : tracks_) {
        if (track->instrument) track->instrument->reset();
        engine::reset_chain(track->chain);
        // The instruments have let go of every voice, so nothing is owed a
        // release any more.
        track->sounding.fill(0);
        track->peak.store(0.0F, std::memory_order_relaxed);
    }
    engine::reset_buses(*buses_);
    // The click sounding and any count-in are forgotten with the rest.
    engine::reset_metronome(*metronome_);
    master_peak_.store(0.0F, std::memory_order_relaxed);
    // A bounce plays the lanes, not what was latched while playing before it.
    for (auto& track : tracks_) engine::release_automation(track->automation);
    if (live_ != nullptr) engine::release_parameter_holds(live_->automation);
}

std::uint32_t SongEngine::output_latency() const noexcept {
    return buses_->track_latency + buses_->return_latency + buses_->master.latency;
}

std::uint64_t SongEngine::effect_tail_samples() const noexcept { return buses_->tail; }

std::size_t SongEngine::return_count() const noexcept { return buses_->returns.size(); }

float SongEngine::return_peak(std::size_t bus) const {
    if (bus >= buses_->returns.size()) return 0.0F;
    return buses_->returns[bus]->peak.load(std::memory_order_relaxed);
}

bool SongEngine::update_processor_state(ProcessorAddress where,
                                        std::span<const std::byte> state) {
    auto* instance = processor(where);
    if (instance == nullptr || !instance->accepts_state_while_running()) return false;
    return instance->load_state(state);
}

void SongEngine::set_instrument(std::size_t track, std::unique_ptr<PluginInstance> instrument) {
    set_processor(track_instrument(static_cast<std::uint32_t>(track)), std::move(instrument));
}

bool SongEngine::has_instrument(std::size_t track) const {
    return track < tracks_.size() && tracks_[track]->instrument != nullptr;
}

// What the render callback is currently playing for one track.
const std::vector<TimedPluginEvent>& SongEngine::timeline_for(std::size_t track) const noexcept {
    if (live_ == nullptr || track >= live_->timelines.size()) return empty_timeline();
    return live_->timelines[track];
}

bool SongEngine::prepare(const Song& song, double sample_rate,
                         std::uint32_t maximum_block_size, std::uint64_t seed,
                         std::string* error, const AudioAssets& assets,
                         const ClipRenditions& renditions) {
    const auto fail = [error](const char* message) {
        if (error != nullptr) *error = message;
        return false;
    };
    if (!std::isfinite(sample_rate) || sample_rate <= 0.0 || maximum_block_size == 0)
        return fail("invalid playback settings");
    if (!song.tempo.valid()) return fail("invalid tempo map");
    if (!song.consistent()) return fail("song refers to a track or pattern that does not exist");

    while (tracks_.size() < song.tracks.size()) tracks_.push_back(std::make_unique<TrackPlayback>());
    tracks_.resize(song.tracks.size());
    for (auto& track : tracks_)
        if (!track) track = std::make_unique<TrackPlayback>();
    for (auto& slot : arrangements_)
        if (!slot) slot = std::make_unique<Arrangement>();

    maximum_block_ = maximum_block_size;
    sample_rate_ = sample_rate;
    automation_scratch_.assign(engine::automation_event_budget, PluginEvent{});
    merge_scratch_.assign(engine::maximum_events_per_chunk, PluginEvent{});
    strips_.assign(song.tracks.size(), MixerStrip{});
    tap_left_.assign(maximum_block_size, 0.0F);
    tap_right_.assign(maximum_block_size, 0.0F);
    rolled_ = false;

    // Nothing is rendering yet, so the first arrangement is installed directly
    // rather than queued for a callback that is not running.
    if (!compile_into(*arrangements_.front(), song, seed, error, assets, renditions))
        return false;
    live_ = arrangements_.front().get();
    handoff_.store(handoff_state(no_slot, 0, no_slot), std::memory_order_release);
    song_samples_ = live_->song_samples;
    published_song_samples_.store(song_samples_, std::memory_order_release);
    published_clock_ = live_->clock;

    for (std::size_t index = 0; index < tracks_.size(); ++index) {
        auto& track = *tracks_[index];
        track.cursor = 0;
        track.left.assign(maximum_block_size, 0.0F);
        track.right.assign(maximum_block_size, 0.0F);
        // The chunk's events live here rather than on the callback's stack.
        track.events.assign(engine::maximum_events_per_chunk, PluginEvent{});
        engine::release_automation(track.automation);
        track.peak.store(0.0F, std::memory_order_relaxed);
        if (track.instrument && !track.instrument->activate(sample_rate, 1, maximum_block_size))
            return fail("an instrument refused to activate");
    }
    // The insert chains, sends, returns and master inserts, shaped to the
    // song, activated, and compensated for their latency.
    if (!engine::prepare_effects(song, track_chains(), *buses_, sample_rate, maximum_block_size,
                                 error))
        return false;
    // The click sounds and room for the notes a count-in holds (item 3.7).
    engine::prepare_metronome(*metronome_, sample_rate, tracks_.size());
    apply_mix(song);
    sample_position_ = 0;
    published_position_.store(0, std::memory_order_release);
    heard_tick_.store(0, std::memory_order_release);
    requested_position_.store(no_seek, std::memory_order_release);
    cursors_valid_ = false;
    return true;
}

bool SongEngine::compile_into(Arrangement& target, const Song& song,
                              std::uint64_t seed, std::string* error,
                              const AudioAssets& assets,
                              const ClipRenditions& renditions) const {
    const auto fail = [error](const char* message) {
        if (error != nullptr) *error = message;
        return false;
    };
    if (sample_rate_ <= 0.0) return fail("invalid playback settings");
    if (!song.tempo.valid()) return fail("invalid tempo map");
    if (!song.consistent()) return fail("song refers to a track or pattern that does not exist");

    TickClock clock(song.tempo, song.ticks_per_beat(), sample_rate_);
    const double length = clock.sample_at(song.length());
    if (!std::isfinite(length) || length < 0.5 ||
        length >= static_cast<double>(std::numeric_limits<std::int64_t>::max()))
        return fail("song length is outside the supported sample range");
    const auto samples = static_cast<std::uint64_t>(std::llround(length));
    if (samples == 0) return fail("song has no length");

    target.timelines.resize(song.tracks.size());
    for (std::size_t index = 0; index < song.tracks.size(); ++index) {
        target.timelines[index] = compile_timeline(song.arrange(index, seed), clock, samples - 1);
        if (!timeline_density_supported(target.timelines[index]))
            return fail("more than 256 simultaneous events on one track");
    }
    // 5. Audio clips: regions placed by the same clock as the events.
    engine::compile_clip_regions(target.clips, song, clock, assets, renditions);
    // Automation: strip envelopes and parameter lanes, placed by the same
    // clock (item 3.1).
    engine::compile_automation(target.automation, song, clock, samples);
    // The click's beats, placed by the same clock (item 3.7).
    engine::compile_clicks(target.clicks, song.meter, clock, song.length(), samples);
    target.song_samples = samples;
    target.clock = std::move(clock);
    target.ticks_per_beat = song.ticks_per_beat();
    target.meter = song.meter;
    return true;
}

bool SongEngine::recompile(const Song& song, std::uint64_t seed, std::string* error,
                           const AudioAssets& assets, const ClipRenditions& renditions) {
    const auto fail = [error](const char* message) {
        if (error != nullptr) *error = message;
        return false;
    };
    auto state = handoff_.load(std::memory_order_acquire);
    if (field(state, rendering_shift) == no_slot)
        return fail("prepare the song before recompiling it");
    // A different set of tracks needs instruments this engine was never given,
    // so the caller is told to rebuild rather than being handed a graph that
    // plays the wrong thing.
    if (song.tracks.size() != tracks_.size())
        return fail("the song has a different track list than the prepared graph");

    // Fill a slot the render callback is neither playing, nor about to pick
    // up, nor still reading while it moves onto another. All three are read
    // from one word, so a take cannot land between two reads and leave the
    // slot it took looking free. The callback only ever moves a slot out of
    // the queued field into the others, and out of the others to free, so a
    // slot free now stays free until this thread queues it.
    std::uint32_t target = no_slot;
    for (std::uint32_t slot = 0; slot < arrangements_.size(); ++slot)
        if (arrangements_[slot] && slot != field(state, queued_shift) &&
            slot != field(state, rendering_shift) && slot != field(state, retiring_shift)) {
            target = slot;
            break;
        }
    if (target == no_slot) return fail("no free arrangement slot");
    auto& arranged = *arrangements_[target];
    if (!compile_into(arranged, song, seed, error, assets, renditions)) return false;

    published_clock_ = arranged.clock;
    published_song_samples_.store(arranged.song_samples, std::memory_order_release);
    // Queued in place of whatever was queued before and not yet taken, which
    // the callback never touched and is free again from here.
    state = handoff_.load(std::memory_order_acquire);
    while (!handoff_.compare_exchange_weak(state, with_field(state, queued_shift, target),
                                           std::memory_order_acq_rel,
                                           std::memory_order_acquire)) {
    }
    apply_mix(song);
    return true;
}

void SongEngine::take_queued_arrangement(bool seeked) noexcept {
    // One step claims the queued slot as the one rendering and keeps the one
    // rendered until now as retiring: both stay out of a recompile's reach
    // while this function reads them.
    auto state = handoff_.load(std::memory_order_acquire);
    do {
        if (field(state, queued_shift) == no_slot) return;
    } while (!handoff_.compare_exchange_weak(
        state,
        handoff_state(no_slot, field(state, queued_shift), field(state, rendering_shift)),
        std::memory_order_acq_rel, std::memory_order_acquire));
    auto* incoming = arrangements_[field(state, queued_shift)].get();
    if (handoff_probe_ != nullptr) handoff_probe_(handoff_probe_context_);
    // A new tempo is a new place for every tick, not a new place in the music:
    // the playhead stays on the tick it had reached and moves to the sample
    // where that tick now falls. A seek taken in this same block was placed
    // with the new clock already and is left where it was put.
    if (!seeked && live_ != nullptr && song_samples_ > 0 && incoming->song_samples > 0 &&
        !(live_->clock == incoming->clock)) {
        const auto position = sample_position_ % song_samples_;
        const double tick = live_->clock.tick_at(static_cast<double>(position));
        // The same rule the timeline was compiled with: a playhead that was
        // on an event's tick is on that event's sample, not one past it.
        const auto kept = sample_for_tick(incoming->clock, tick) % incoming->song_samples;
        sample_position_ = kept;
        published_position_.store(kept, std::memory_order_release);
    }
    live_ = incoming;
    song_samples_ = incoming->song_samples;
    // Done with the old slot: it is free for the next recompile.
    handoff_.fetch_or(no_slot << retiring_shift, std::memory_order_acq_rel);
    // The events under the playhead are not the ones that were under it a
    // moment ago, so every cursor is found again before the next block.
    cursors_valid_ = false;
    // A note whose step was erased while it sounded has lost the note-off that
    // would have ended it, and a note that began before the playhead is not
    // re-struck by the new timeline either. Both are let go here, so an edit
    // cannot leave a voice held on an instrument for ever.
    release_arrangement_notes_ = true;
}

std::uint64_t SongEngine::heard_position(std::uint64_t song_position) const noexcept {
    if (song_samples_ == 0) return song_position;
    const auto latency = static_cast<std::uint64_t>(output_latency()) % song_samples_;
    const auto position = song_position % song_samples_;
    return position >= latency ? position - latency : song_samples_ - (latency - position);
}

void SongEngine::set_strip(std::size_t track, const MixerStrip& strip, bool any_solo) {
    if (track >= tracks_.size()) return;
    if (strips_.size() < tracks_.size()) strips_.resize(tracks_.size());
    strips_[track] = strip;
    any_solo_ = any_solo;
    const auto gain = strip_gain(strip, any_solo);
    auto& playback = *tracks_[track];
    playback.gain_left.store(gain.left, std::memory_order_relaxed);
    playback.gain_right.store(gain.right, std::memory_order_relaxed);
    // The same strip, control by control, for an automated track: its lanes
    // replace some controls and the rest play these (item 3.1). Every
    // conversion is made here, on the control thread.
    const double linear = db_to_linear(strip.gain_db);
    const double angle = (std::clamp(strip.pan, -1.0, 1.0) + 1.0) * 0.25 * std::numbers::pi;
    auto& automation = playback.automation;
    automation.fader.store(static_cast<float>(linear), std::memory_order_relaxed);
    automation.pan_left.store(static_cast<float>(std::cos(angle)), std::memory_order_relaxed);
    automation.pan_right.store(static_cast<float>(std::sin(angle)), std::memory_order_relaxed);
    automation.unmuted.store(strip.mute ? 0.0F : 1.0F, std::memory_order_relaxed);
    automation.solo_gate.store(!any_solo || strip.solo ? 1.0F : 0.0F, std::memory_order_relaxed);
    // What the post-fader sends take, so a move reaches them too.
    const bool heard = audible(strip, any_solo);
    playback.chain.fader.store(heard ? static_cast<float>(linear) : 0.0F,
                               std::memory_order_relaxed);
    playback.chain.audible.store(heard, std::memory_order_relaxed);
}

bool SongEngine::move(const StripMove& move) {
    if (move.track >= tracks_.size()) return false;
    if (strips_.size() < tracks_.size()) strips_.resize(tracks_.size());
    auto strip = strips_[move.track];
    switch (move.control) {
    case StripControl::gain: strip.gain_db = move.value; break;
    case StripControl::pan: strip.pan = std::clamp(move.value, -1.0, 1.0); break;
    case StripControl::mute: strip.mute = move.value >= 0.5; break;
    }
    set_strip(move.track, strip, any_solo_);
    return moves_.push(move);
}

void SongEngine::apply_mix(const Song& song) {
    const bool solo = song.any_solo();
    for (std::size_t index = 0; index < song.tracks.size() && index < tracks_.size(); ++index)
        set_strip(index, song.tracks[index].mix, solo);
    set_master_gain_db(song.master_gain_db);
    engine::apply_effect_mix(song, track_chains(), *buses_);
    set_metronome(song.metronome.enabled, song.metronome.level_db);
}

void SongEngine::set_master_gain_db(double decibels) {
    master_gain_.store(static_cast<float>(db_to_linear(decibels)), std::memory_order_relaxed);
}

float SongEngine::track_peak(std::size_t track) const {
    if (track >= tracks_.size()) return 0.0F;
    return tracks_[track]->peak.load(std::memory_order_relaxed);
}

std::vector<std::byte> SongEngine::save_track_state(std::size_t track) {
    if (!has_instrument(track)) return {};
    return tracks_[track]->instrument->save_state();
}

bool SongEngine::load_track_state(std::size_t track, std::span<const std::byte> state) {
    return has_instrument(track) && tracks_[track]->instrument->load_state(state);
}

void SongEngine::seek_cursors(std::uint64_t position) noexcept {
    // Jumping the playhead skips whatever note-offs lay between where it was
    // and where it is, so the same release the arrangement swap needs is owed
    // here too.
    release_arrangement_notes_ = true;
    for (std::size_t index = 0; index < tracks_.size(); ++index) {
        const auto& timeline = timeline_for(index);
        const auto found = std::lower_bound(
            timeline.begin(), timeline.end(), position,
            [](const TimedPluginEvent& event, std::uint64_t sample) {
                return event.sample < sample;
            });
        tracks_[index]->cursor = static_cast<std::size_t>(found - timeline.begin());
    }
    // Every automation lane is chased to the new position (item 3.1).
    if (live_ != nullptr) engine::seek_automation(live_->automation, position);
}

bool SongEngine::play_live(std::size_t track, const PluginEvent& event) noexcept {
    if (track >= tracks_.size() || !tracks_[track]) return false;
    // A full queue drops the note rather than waiting: the audio thread must
    // never be held up by a keyboard.
    return tracks_[track]->live.push(event);
}

bool SongEngine::perform(std::size_t track, const PluginEvent& event) noexcept {
    if (track >= tracks_.size() || !tracks_[track]) return false;
    return performed_.push({static_cast<std::uint32_t>(track), event});
}

std::size_t SongEngine::collect_events(TrackPlayback& track, std::size_t index,
                                       std::uint64_t song_position, std::uint64_t end,
                                       bool from_timeline, bool capture,
                                       std::uint64_t capture_sample, Tick capture_tick) noexcept {
    auto& events = track.events;
    const auto capacity = events.size();
    const auto& timeline = timeline_for(index);
    std::size_t count = 0;
    // Notes the arrangement still owes a release come first.
    if (release_arrangement_notes_) {
        for (std::size_t key = 0; key < track.sounding.size(); ++key) {
            if (track.sounding[key] > 0 && count < capacity) {
                events[count] = {PluginEvent::Type::note_off, 0,
                                 static_cast<std::int32_t>(key), 0.0, 0.0};
                ++count;
                // A key-addressed release matches every voice of this key.
                track.sounding[key] = 0;
            }
        }
    }
    // Live events are due now, before any future event in this window.
    while (count < capacity && track.live.pop(events[count])) {
        events[count].sample_offset = 0;
        ++count;
    }
    for (std::size_t arrived = 0; arrived < incoming_count_; ++arrived) {
        const auto& routed = incoming_[arrived];
        if (routed.track != index || count >= capacity) continue;
        events[count] = routed.event;
        events[count].sample_offset = 0;
        ++count;
        // Captured where it was played: against what the performer heard as
        // this block began, which the speakers sounded the output latency
        // after the callback rendered it (capture_sample). A full capture
        // ring loses the note from the take, never from the speakers.
        // The tick is stamped here, with the clock this block is played
        // under: a recompile published later must not re-read it.
        if (capture)
            (void)captured_.push(
                {routed.track, capture_sample, capture_tick, events[count - 1]});
    }
    const std::size_t timeline_begin = count;
    while (from_timeline && track.cursor < timeline.size() &&
           timeline[track.cursor].sample < end) {
        const auto& timed = timeline[track.cursor];
        if (timed.sample >= song_position && count < capacity) {
            events[count] = timed.event;
            events[count].sample_offset = static_cast<std::uint32_t>(timed.sample - song_position);
            const auto key = timed.event.key_or_parameter;
            if (key >= 0 && key < static_cast<std::int32_t>(track.sounding.size())) {
                auto& held = track.sounding[static_cast<std::size_t>(key)];
                if (timed.event.type == PluginEvent::Type::note_on) {
                    if (held < 255) ++held;
                } else if (timed.event.type == PluginEvent::Type::note_off && held > 0) {
                    --held;
                }
            }
            ++count;
        }
        ++track.cursor;
    }
    // The instrument's automation lanes (item 3.1), merged into the
    // timeline's events in sample order: a lane's value on a sample goes
    // before a note on it, as a parameter lock does.
    const auto& automation = live_ != nullptr ? live_->automation : nothing_arranged.automation;
    if (from_timeline && index < automation.instrument_lanes.size() &&
        automation.instrument_lanes[index] >= 0) {
        const auto& lanes =
            automation.processors[static_cast<std::size_t>(automation.instrument_lanes[index])];
        const auto added = engine::automation_events(lanes, song_position, end,
                                                     automation_scratch_);
        if (added > 0) {
            const auto timed = count - timeline_begin;
            std::copy_n(events.begin() + static_cast<std::ptrdiff_t>(timeline_begin), timed,
                        merge_scratch_.begin());
            std::size_t from_events = 0;
            std::size_t from_lanes = 0;
            count = timeline_begin;
            while (count < capacity && (from_events < timed || from_lanes < added)) {
                const bool lane_first =
                    from_lanes < added &&
                    (from_events >= timed || automation_scratch_[from_lanes].sample_offset <=
                                                 merge_scratch_[from_events].sample_offset);
                events[count++] = lane_first ? automation_scratch_[from_lanes++]
                                             : merge_scratch_[from_events++];
            }
        }
    }
    return count;
}

void SongEngine::drain_edits(PluginInstance& processor, ProcessorAddress where,
                             std::uint64_t song_position, bool from_timeline) noexcept {
    // A plugin reports at most a scratch-full per call; one that fills it is
    // asked again, and one that keeps filling it is not allowed to hold the
    // callback: the loop is bounded.
    for (int round = 0; round < 4; ++round) {
        const auto taken = std::min(processor.take_parameter_edits(edit_scratch_),
                                    edit_scratch_.size());
        for (std::size_t index = 0; index < taken; ++index) {
            const auto& edit = edit_scratch_[index];
            (void)edits_.push({where, song_position + edit.sample_offset, from_timeline, edit});
            // A knob held in the plugin's own window holds its lane (item 3.1).
            if (from_timeline && live_ != nullptr)
                engine::note_parameter_edit(live_->automation, where, edit);
        }
        if (taken < edit_scratch_.size()) return;
    }
}

namespace {
// What an insert slot's edits are stamped with: the chunk they came out of.
struct DrainContext {
    SongEngine* engine;
    std::uint64_t song_position;
    bool rolling;
    // The chunk's end, what it plays, and where an insert's automation
    // events are written (item 3.1).
    std::uint64_t end;
    const engine::ArrangementAutomation* automation;
    std::span<PluginEvent> scratch;
};
} // namespace

void SongEngine::drain_insert_edits(void* context, PluginInstance& processor,
                                    ProcessorAddress where) noexcept {
    auto& chunk = *static_cast<DrainContext*>(context);
    chunk.engine->drain_edits(processor, where, chunk.song_position, chunk.rolling);
}

std::span<const PluginEvent> SongEngine::insert_events(void* context,
                                                       ProcessorAddress where) noexcept {
    auto& chunk = *static_cast<DrainContext*>(context);
    if (!chunk.rolling || chunk.automation == nullptr) return {};
    const auto* lanes = engine::lanes_for(*chunk.automation, where);
    if (lanes == nullptr) return {};
    const auto count =
        engine::automation_events(*lanes, chunk.song_position, chunk.end, chunk.scratch);
    return chunk.scratch.first(count);
}

TransportInfo SongEngine::transport_at(const Arrangement& arranged, std::uint64_t song_position,
                                       bool playing) const noexcept {
    const auto sample = static_cast<double>(song_position);
    const double tick = arranged.clock.tick_at(sample);
    const auto beat_ticks = static_cast<double>(std::max<Tick>(1, arranged.ticks_per_beat));
    const auto bar = arranged.meter.bar_at(static_cast<Tick>(tick));
    const auto& meter = arranged.meter.meter_in(bar);
    return {arranged.clock.bpm_at_sample(sample), tick / beat_ticks, bar, meter.numerator,
            meter.denominator, playing};
}

void SongEngine::process_chunk(StereoBlock output, const InputBlock& input,
                               std::uint64_t song_position, bool from_timeline) noexcept {
    const auto frames = output.left.size();
    std::fill(output.left.begin(), output.left.end(), 0.0F);
    std::fill(output.right.begin(), output.right.end(), 0.0F);
    const auto end = song_position + frames;

    // Whatever a MIDI port and the on-screen surfaces delivered since the last
    // block, one event per track it is routed to. It is taken once,
    // before any track renders, and handed to the tracks it names below.
    incoming_count_ = engine::gather_input(input_.load(std::memory_order_acquire), performed_,
                                           incoming_);
    const bool capture = from_timeline && recording_.load(std::memory_order_acquire);
    const Arrangement& arranged = live_ != nullptr ? *live_ : nothing_arranged;
    // What the performer heard as this block began: the song, the output
    // latency ago (decision 10 compensates every path to it).
    const std::uint64_t capture_sample = capture ? heard_position(song_position) : 0;
    const Tick capture_tick = capture ? tick_at_sample(arranged.clock, capture_sample) : 0;
    // Where the song is for every processor this chunk: a tempo-synced effect
    // follows the tempo map from here (plan C20).
    const TransportInfo transport = transport_at(arranged, song_position, from_timeline);
    // Recorded input goes here only while the song plays and records.
    SampleRing* const capture_ring = capture ? capture_.load(std::memory_order_acquire) : nullptr;
    DrainContext drain_context{this, song_position, from_timeline, end, &arranged.automation,
                               std::span<PluginEvent>(automation_scratch_)};
    const engine::EditDrain drain{&SongEngine::drain_insert_edits, &drain_context,
                                  &SongEngine::insert_events};
    engine::begin_buses(*buses_, frames);

    for (std::size_t index = 0; index < tracks_.size(); ++index) {
        auto& track = *tracks_[index];
        // A track added after prepare() has no buffers yet and is not played.
        if (track.left.size() < frames || track.right.size() < frames) continue;
        // 1. Events: owed releases, live, routed input, then the timeline.
        const auto count =
            collect_events(track, index, song_position, end, from_timeline, capture,
                           capture_sample, capture_tick);
        // 2. The track's buffer starts silent, with or without an instrument,
        // because the stages after the instrument's still add to it.
        const std::span<float> left{track.left.data(), frames};
        const std::span<float> right{track.right.data(), frames};
        std::fill(left.begin(), left.end(), 0.0F);
        std::fill(right.begin(), right.end(), 0.0F);
        const StereoBlock buffer{left, right};
        if (track.instrument) {
            // 3. The instrument renders in place.
            track.instrument->set_transport(transport);
            track.instrument->process(buffer, std::span{track.events.data(), count});
            // 4. What it reported about its own parameters goes to the ring.
            drain_edits(*track.instrument, track_instrument(static_cast<std::uint32_t>(index)),
                        song_position, from_timeline);
        }
        // 5. Audio-clip regions, from the arrangement only.
        engine::sum_clip_regions(track.clips, arranged.clips, index, buffer, song_position,
                                 from_timeline);
        // 6. Input monitoring; a capture taps the raw input here.
        engine::add_input_monitoring(track.input, buffer, input, capture_ring,
                                     static_cast<std::uint32_t>(index), song_position);
        // 7. The insert chain, in place. 8. Its compensation delay.
        engine::run_insert_chain(track.chain, buffer, BusKind::track,
                                 static_cast<std::uint32_t>(index), transport, drain);
        engine::apply_track_compensation(track.chain, buffer);
        // 9. The strip gain for this chunk: the static gain, or the
        // automation envelope's ramp times the solo gate.
        const auto ramp = engine::chunk_strip_gain(
            track.automation, arranged.automation, index,
            {track.gain_left.load(std::memory_order_relaxed),
             track.gain_right.load(std::memory_order_relaxed)},
            song_position, frames, from_timeline);
        // 10. Sends: post-fader ones follow the automated fader.
        if (ramp.automated)
            engine::mix_sends_ramp(track.chain, *buses_, buffer, ramp.fader_from, ramp.fader_to,
                                   ramp.audible_from, ramp.audible_to);
        else
            engine::mix_sends(track.chain, *buses_, buffer);
        // 11. Onto the direct bus.
        const float peak = ramp.automated ? engine::mix_into_ramp(output, left, right, ramp)
                                          : mix_into(output, left, right, ramp.from);
        // A block split by the loop point arrives as two chunks; the meter must
        // report the loudest of them, not whichever happened to be last.
        track.peak.store(std::max(track.peak.load(std::memory_order_relaxed), peak),
                         std::memory_order_relaxed);
        for (std::size_t frame = 0; frame < frames; ++frame) {
            const float fraction = static_cast<float>(frame) / static_cast<float>(frames);
            left[frame] *= ramp.from.left + (ramp.to.left - ramp.from.left) * fraction;
            right[frame] *= ramp.from.right + (ramp.to.right - ramp.from.right) * fraction;
        }
        tap_output({BusKind::track, static_cast<std::uint32_t>(index)}, buffer,
                   song_position, from_timeline);
    }
    release_arrangement_notes_ = false;
    engine::process_returns(*buses_, frames, transport, drain);
    for (std::size_t index = 0; index < buses_->returns.size(); ++index) {
        auto& bus = *buses_->returns[index];
        // Return buffers still hold the pre-strip signal; their summed bus
        // already contains its gain, so this readout cannot alter the mix.
        for (std::size_t frame = 0; frame < frames; ++frame) {
            bus.left[frame] *= bus.gain_left.load(std::memory_order_relaxed);
            bus.right[frame] *= bus.gain_right.load(std::memory_order_relaxed);
        }
        tap_output({BusKind::ret, static_cast<std::uint32_t>(index)},
                   {{bus.left.data(), frames}, {bus.right.data(), frames}},
                   song_position, from_timeline);
    }
    engine::apply_master_compensation(*buses_, output);
    engine::run_master_inserts(*buses_, output, transport, drain);
    const float bus_peak = apply_master(output, master_gain_.load(std::memory_order_relaxed));
    master_peak_.store(std::max(master_peak_.load(std::memory_order_relaxed), bus_peak),
                       std::memory_order_relaxed);
    // The guide click is added later, outside the song mix. Resampling the
    // master records the mix, matching the default export.
    tap_output({BusKind::master, 0}, output, song_position, from_timeline);
    if (bounce_tap_) {
        std::copy_n(tap_left_.begin(), frames, output.left.begin());
        std::copy_n(tap_right_.begin(), frames, output.right.begin());
    }
}

void SongEngine::set_audio_input(std::size_t track, const AudioInputRoute& route) noexcept {
    if (track >= tracks_.size() || !tracks_[track]) return;
    tracks_[track]->input.route.store(engine::InputPlayback::pack(route),
                                      std::memory_order_release);
}

AudioInputRoute SongEngine::audio_input(std::size_t track) const noexcept {
    if (track >= tracks_.size() || !tracks_[track]) return {};
    return engine::InputPlayback::unpack(
        tracks_[track]->input.route.load(std::memory_order_acquire));
}

void SongEngine::set_audio_inputs(const std::vector<AudioInputRoute>& routes) noexcept {
    for (std::size_t track = 0; track < tracks_.size(); ++track)
        set_audio_input(track, track < routes.size() ? routes[track] : AudioInputRoute{});
}

void SongEngine::process(StereoBlock output) noexcept { process(output, InputBlock{}); }

void SongEngine::process(StereoBlock output, InputBlock input) noexcept {
    if (output.left.size() != output.right.size()) return;
    if (input.frames != output.left.size()) input = {};
    const auto requested = requested_position_.exchange(no_seek, std::memory_order_acq_rel);
    const bool seeked = requested != no_seek;
    if (seeked) {
        sample_position_ = requested;
        published_position_.store(requested, std::memory_order_release);
        cursors_valid_ = false;
        release_arrangement_notes_ = true;
    }
    const bool stopped = stop_requested_.exchange(false, std::memory_order_acq_rel);
    if (stopped) {
        release_arrangement_notes_ = true;
        // A count-in stopped before it ends starts nothing.
        metronome_->count_clicking = false;
        metronome_->song_waiting = false;
        metronome_->counting_in.store(false, std::memory_order_release);
    }
    // An edit made while the song was playing is picked up here, at a block
    // boundary, so the arrangement changes underneath the playhead rather than
    // the playhead being sent back to the start of the song.
    take_queued_arrangement(seeked);
    const bool rolling = is_playing() && song_samples_ > 0;
    // Stopping ends every latch and every plugin hold; starting to play
    // chases every lane (item 3.1).
    if (stopped || (!rolling && rolled_)) {
        for (auto& track : tracks_) engine::release_automation(track->automation);
        if (live_ != nullptr) engine::release_parameter_holds(live_->automation);
    }
    if (rolling && !rolled_) cursors_valid_ = false;
    rolled_ = rolling;
    // Strip moves from the interface: each sets its control's touch bit and,
    // while the song plays, goes back stamped with where it was heard.
    StripMove moved;
    while (moves_.pop(moved)) {
        if (moved.track < tracks_.size())
            engine::apply_strip_touch(tracks_[moved.track]->automation,
                                      static_cast<std::size_t>(moved.control), moved.touching,
                                      rolling);
        if (rolling && live_ != nullptr) {
            const auto at = sample_position_ % song_samples_;
            (void)strip_moves_.push({moved, at, tick_at_sample(live_->clock, at)});
        }
    }
    master_peak_.store(0.0F, std::memory_order_relaxed);
    for (auto& track : tracks_) track->peak.store(0.0F, std::memory_order_relaxed);
    for (auto& bus : buses_->returns) bus->peak.store(0.0F, std::memory_order_relaxed);
    if (!is_playing() || song_samples_ == 0) {
        // A stopped transport is not a silent instrument: keys pressed in the
        // interface still sound, and what is already ringing keeps ringing.
        if (maximum_block_ == 0) {
            std::fill(output.left.begin(), output.left.end(), 0.0F);
            std::fill(output.right.begin(), output.right.end(), 0.0F);
            return;
        }
        // Edits a plugin reports on a stopped song are stamped where the
        // playhead rests.
        const auto resting = song_samples_ == 0 ? 0 : sample_position_ % song_samples_;
        metronome_->was_rolling = false;
        std::size_t idle = 0;
        while (idle < output.left.size()) {
            const auto frames = std::min<std::size_t>(output.left.size() - idle,
                                                      maximum_block_);
            const StereoBlock chunk{output.left.subspan(idle, frames),
                                    output.right.subspan(idle, frames)};
            process_chunk(chunk, input.slice(idle, frames), resting, false);
            // A click stopped mid-sound finishes; no new one starts.
            engine::render_clicks(*metronome_, chunk, {});
            idle += frames;
        }
        // Stopped, the listener hears the song where the playhead rests.
        if (live_ != nullptr && song_samples_ > 0)
            heard_tick_.store(tick_at_sample(live_->clock, resting), std::memory_order_release);
        return;
    }
    // Play pressed with a count-in asked for: the click counts in from here,
    // and the song waits where it is until the count-in is over (item 3.7).
    if (const auto bars = metronome_->count_in_request.exchange(0, std::memory_order_acq_rel);
        bars > 0)
        start_count_in(bars);
    std::size_t rendered = 0;
    while (metronome_->song_waiting && rendered < output.left.size()) {
        // The playhead rests; instruments still play what is played into
        // them, but nothing is captured.
        const auto resting = sample_position_ % song_samples_;
        const auto frames = static_cast<std::size_t>(std::min<std::uint64_t>(
            {output.left.size() - rendered, engine::count_in_remaining(*metronome_),
             maximum_block_}));
        if (frames == 0) {
            finish_count_in();
            break;
        }
        const StereoBlock chunk{output.left.subspan(rendered, frames),
                                output.right.subspan(rendered, frames)};
        process_chunk(chunk, input.slice(rendered, frames), resting, false);
        engine::hold_count_in_notes(*metronome_,
                                    std::span<const RoutedEvent>{incoming_.data(), incoming_count_});
        engine::render_clicks(*metronome_, chunk, {});
        rendered += frames;
        if (engine::count_in_remaining(*metronome_) == 0) finish_count_in();
    }
    if (rendered < output.left.size()) {
        // The song's click waits out the compensation after every start and
        // every seek, so it never sounds a beat the listener is not reaching.
        if (seeked || !metronome_->was_rolling) metronome_->rolled = 0;
        metronome_->was_rolling = true;
    }
    while (rendered < output.left.size()) {
        const auto song_position = sample_position_ % song_samples_;
        if (!cursors_valid_ || song_position != continuous_from_) {
            seek_cursors(song_position);
            cursors_valid_ = true;
        }
        const auto until_wrap = song_samples_ - song_position;
        // A chunk never exceeds what prepare() sized the per-track buffers for,
        // however long a block the host asks for.
        auto frames = std::min<std::uint64_t>(
            {output.left.size() - rendered, until_wrap, maximum_block_});
        for (std::size_t track = 0; track < tracks_.size(); ++track)
            frames = timeline_window(timeline_for(track), song_position, frames);
        // An automated strip ramps between two grid points of its envelope,
        // so a chunk never spans one.
        if (live_ != nullptr && live_->automation.any_strip)
            frames = std::min<std::uint64_t>(
                frames, engine::automation_grid - song_position % engine::automation_grid);
        const StereoBlock chunk{output.left.subspan(rendered, frames),
                                output.right.subspan(rendered, frames)};
        process_chunk(chunk, input.slice(rendered, static_cast<std::size_t>(frames)),
                      song_position, true);
        // The click, on the beats the listener reaches in this chunk.
        engine::render_clicks(*metronome_, chunk,
                              {live_ != nullptr ? &live_->clicks : nullptr,
                               heard_position(song_position), song_samples_, output_latency(),
                               true});
        rendered += static_cast<std::size_t>(frames);
        sample_position_ += frames;
        continuous_from_ = song_position + frames;
    }
    published_position_.store(sample_position_, std::memory_order_release);
    // What the listener hears now, read with the clock this block was played
    // under, for a take that ends before the next block (heard_tick()).
    if (live_ != nullptr)
        heard_tick_.store(tick_at_sample(live_->clock, heard_position(sample_position_)),
                          std::memory_order_release);
}

} // namespace blokkily
