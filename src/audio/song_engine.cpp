#include "blokkily/audio/song_engine.hpp"

#include "engine/arrangement.hpp"
#include "engine/engine_automation.hpp"
#include "engine/engine_clips.hpp"
#include "engine/engine_effects.hpp"
#include "engine/engine_input.hpp"
#include "engine/track_playback.hpp"

#include <algorithm>
#include <array>
#include <cmath>

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
} // namespace

SongEngine::SongEngine() : buses_(std::make_unique<engine::BusPlayback>()) {}
SongEngine::~SongEngine() = default;

std::unique_ptr<PluginInstance>* SongEngine::processor_slot(ProcessorAddress where) const {
    // Only a track's instrument has somewhere to live until the insert chains
    // and the return buses exist.
    if (where.kind != BusKind::track || !where.instrument() || where.bus >= tracks_.size())
        return nullptr;
    return &tracks_[where.bus]->instrument;
}

void SongEngine::set_processor(ProcessorAddress where, std::unique_ptr<PluginInstance> instance) {
    if (where.kind == BusKind::track && where.instrument())
        while (tracks_.size() <= where.bus) tracks_.push_back(std::make_unique<TrackPlayback>());
    if (auto* slot = processor_slot(where)) *slot = std::move(instance);
}

PluginInstance* SongEngine::processor(ProcessorAddress where) const {
    const auto* slot = processor_slot(where);
    return slot == nullptr ? nullptr : slot->get();
}

std::vector<ReleasedProcessor> SongEngine::release_processors() {
    std::vector<ReleasedProcessor> released;
    for (std::size_t index = 0; index < tracks_.size(); ++index) {
        auto& instrument = tracks_[index]->instrument;
        if (instrument)
            released.push_back({track_instrument(static_cast<std::uint32_t>(index)),
                                std::move(instrument)});
    }
    return released;
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
                         std::string* error) {
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

    // Nothing is rendering yet, so the first arrangement is installed directly
    // rather than queued for a callback that is not running.
    if (!compile_into(*arrangements_.front(), song, seed, error)) return false;
    live_ = arrangements_.front().get();
    queued_.store(nullptr, std::memory_order_release);
    rendering_.store(live_, std::memory_order_release);
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
        track.peak.store(0.0F, std::memory_order_relaxed);
        if (track.instrument && !track.instrument->activate(sample_rate, 1, maximum_block_size))
            return fail("an instrument refused to activate");
    }
    apply_mix(song);
    sample_position_ = 0;
    published_position_.store(0, std::memory_order_release);
    requested_position_.store(no_seek, std::memory_order_release);
    cursors_valid_ = false;
    return true;
}

bool SongEngine::compile_into(Arrangement& target, const Song& song,
                              std::uint64_t seed, std::string* error) const {
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
    target.song_samples = samples;
    target.clock = std::move(clock);
    return true;
}

bool SongEngine::recompile(const Song& song, std::uint64_t seed, std::string* error) {
    const auto fail = [error](const char* message) {
        if (error != nullptr) *error = message;
        return false;
    };
    if (live_ == nullptr && rendering_.load(std::memory_order_acquire) == nullptr)
        return fail("prepare the song before recompiling it");
    // A different set of tracks needs instruments this engine was never given,
    // so the caller is told to rebuild rather than being handed a graph that
    // plays the wrong thing.
    if (song.tracks.size() != tracks_.size())
        return fail("the song has a different track list than the prepared graph");

    // Fill a slot the render callback is neither playing nor about to pick up.
    // The queued slot is read before the rendering one: the callback moves a
    // slot from queued to rendering, never back, so reading in this order can
    // only see it in one of the two places and never in neither. Read the
    // other way round, a take landing between the two reads would leave the
    // slot it took looking free while the callback plays it.
    const auto* queued = queued_.load(std::memory_order_acquire);
    const auto* rendering = rendering_.load(std::memory_order_acquire);
    Arrangement* target = nullptr;
    for (auto& slot : arrangements_)
        if (slot && slot.get() != rendering && slot.get() != queued) {
            target = slot.get();
            break;
        }
    if (target == nullptr) return fail("no free arrangement slot");
    if (!compile_into(*target, song, seed, error)) return false;

    published_clock_ = target->clock;
    published_song_samples_.store(target->song_samples, std::memory_order_release);
    queued_.store(target, std::memory_order_release);
    apply_mix(song);
    return true;
}

void SongEngine::take_queued_arrangement(bool seeked) noexcept {
    auto* incoming = queued_.exchange(nullptr, std::memory_order_acquire);
    if (incoming == nullptr) return;
    // A new tempo is a new place for every tick, not a new place in the music:
    // the playhead stays on the tick it had reached and moves to the sample
    // where that tick now falls. A seek taken in this same block was placed
    // with the new clock already and is left where it was put.
    if (!seeked && live_ != nullptr && song_samples_ > 0 && incoming->song_samples > 0 &&
        !(live_->clock == incoming->clock)) {
        const auto position = sample_position_ % song_samples_;
        const double tick = live_->clock.tick_at(static_cast<double>(position));
        const double moved = std::max(0.0, std::round(incoming->clock.sample_at_tick(tick)));
        const auto kept = static_cast<std::uint64_t>(moved) % incoming->song_samples;
        sample_position_ = kept;
        published_position_.store(kept, std::memory_order_release);
    }
    live_ = incoming;
    rendering_.store(incoming, std::memory_order_release);
    song_samples_ = incoming->song_samples;
    // The events under the playhead are not the ones that were under it a
    // moment ago, so every cursor is found again before the next block.
    cursors_valid_ = false;
    // A note whose step was erased while it sounded has lost the note-off that
    // would have ended it, and a note that began before the playhead is not
    // re-struck by the new timeline either. Both are let go here, so an edit
    // cannot leave a voice held on an instrument for ever.
    release_arrangement_notes_ = true;
}

void SongEngine::set_strip(std::size_t track, const MixerStrip& strip, bool any_solo) {
    if (track >= tracks_.size()) return;
    const auto gain = strip_gain(strip, any_solo);
    tracks_[track]->gain_left.store(gain.left, std::memory_order_relaxed);
    tracks_[track]->gain_right.store(gain.right, std::memory_order_relaxed);
}

void SongEngine::apply_mix(const Song& song) {
    const bool solo = song.any_solo();
    for (std::size_t index = 0; index < song.tracks.size() && index < tracks_.size(); ++index)
        set_strip(index, song.tracks[index].mix, solo);
    set_master_gain_db(song.master_gain_db);
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
}

bool SongEngine::play_live(std::size_t track, const PluginEvent& event) noexcept {
    if (track >= tracks_.size() || !tracks_[track]) return false;
    // A full queue drops the note rather than waiting: the audio thread must
    // never be held up by a keyboard.
    return tracks_[track]->live.push(event);
}

std::size_t SongEngine::collect_events(TrackPlayback& track, std::size_t index,
                                       std::uint64_t song_position, std::uint64_t end,
                                       bool from_timeline, bool capture) noexcept {
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
        // Captured where it was heard: at the start of this block, which is
        // where the instrument is told to sound it. A full capture ring loses
        // the note from the take, never from the speakers.
        if (capture) (void)captured_.push({routed.track, song_position, events[count - 1]});
    }
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
        }
        if (taken < edit_scratch_.size()) return;
    }
}

void SongEngine::process_chunk(StereoBlock output, std::uint64_t song_position,
                               bool from_timeline) noexcept {
    const auto frames = output.left.size();
    std::fill(output.left.begin(), output.left.end(), 0.0F);
    std::fill(output.right.begin(), output.right.end(), 0.0F);
    const auto end = song_position + frames;

    // Whatever a MIDI port delivered since the last block. It is taken once,
    // before any track renders, and handed to the tracks it names below.
    incoming_count_ = 0;
    if (auto* input = input_.load(std::memory_order_acquire))
        while (incoming_count_ < incoming_.size() && input->pop(incoming_[incoming_count_]))
            ++incoming_count_;
    const bool capture = from_timeline && recording_.load(std::memory_order_acquire);
    const Arrangement& arranged = live_ != nullptr ? *live_ : nothing_arranged;

    for (std::size_t index = 0; index < tracks_.size(); ++index) {
        auto& track = *tracks_[index];
        // A track added after prepare() has no buffers yet and is not played.
        if (track.left.size() < frames || track.right.size() < frames) continue;
        // 1. Events: owed releases, live, routed input, then the timeline.
        const auto count =
            collect_events(track, index, song_position, end, from_timeline, capture);
        // 2. The track's buffer starts silent, with or without an instrument,
        // because the stages after the instrument's still add to it.
        const std::span<float> left{track.left.data(), frames};
        const std::span<float> right{track.right.data(), frames};
        std::fill(left.begin(), left.end(), 0.0F);
        std::fill(right.begin(), right.end(), 0.0F);
        const StereoBlock buffer{left, right};
        if (track.instrument) {
            // 3. The instrument renders in place.
            track.instrument->process(buffer, std::span{track.events.data(), count});
            // 4. What it reported about its own parameters goes to the ring.
            drain_edits(*track.instrument, track_instrument(static_cast<std::uint32_t>(index)),
                        song_position, from_timeline);
        }
        // 5. Audio-clip regions, from the arrangement only.
        engine::sum_clip_regions(track.clips, arranged.clips, buffer, song_position,
                                 from_timeline);
        // 6. Input monitoring; a capture taps the raw input here.
        engine::add_input_monitoring(track.input, buffer, song_position, from_timeline);
        // 7. The insert chain, in place. 8. Its compensation delay.
        engine::run_insert_chain(track.chain, buffer);
        engine::apply_track_compensation(track.chain, buffer);
        // 9. The strip gain for this chunk.
        const StripGain gain = engine::chunk_strip_gain(
            track.automation, arranged.automation,
            {track.gain_left.load(std::memory_order_relaxed),
             track.gain_right.load(std::memory_order_relaxed)},
            song_position, frames, from_timeline);
        // 10. Sends.
        engine::mix_sends(track.chain, *buses_, buffer, gain);
        // 11. Onto the direct bus.
        const float peak = mix_into(output, left, right, gain);
        // A block split by the loop point arrives as two chunks; the meter must
        // report the loudest of them, not whichever happened to be last.
        track.peak.store(std::max(track.peak.load(std::memory_order_relaxed), peak),
                         std::memory_order_relaxed);
    }
    release_arrangement_notes_ = false;
    engine::process_returns(*buses_, output);
    engine::apply_master_compensation(*buses_, output);
    engine::run_master_inserts(*buses_, output);
    const float bus_peak = apply_master(output, master_gain_.load(std::memory_order_relaxed));
    master_peak_.store(std::max(master_peak_.load(std::memory_order_relaxed), bus_peak),
                       std::memory_order_relaxed);
}

void SongEngine::process(StereoBlock output) noexcept {
    if (output.left.size() != output.right.size()) return;
    const auto requested = requested_position_.exchange(no_seek, std::memory_order_acq_rel);
    const bool seeked = requested != no_seek;
    if (seeked) {
        sample_position_ = requested;
        published_position_.store(requested, std::memory_order_release);
        cursors_valid_ = false;
        release_arrangement_notes_ = true;
    }
    if (stop_requested_.exchange(false, std::memory_order_acq_rel))
        release_arrangement_notes_ = true;
    // An edit made while the song was playing is picked up here, at a block
    // boundary, so the arrangement changes underneath the playhead rather than
    // the playhead being sent back to the start of the song.
    take_queued_arrangement(seeked);
    master_peak_.store(0.0F, std::memory_order_relaxed);
    for (auto& track : tracks_) track->peak.store(0.0F, std::memory_order_relaxed);
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
        std::size_t idle = 0;
        while (idle < output.left.size()) {
            const auto frames = std::min<std::size_t>(output.left.size() - idle,
                                                      maximum_block_);
            process_chunk({output.left.subspan(idle, frames),
                           output.right.subspan(idle, frames)}, resting, false);
            idle += frames;
        }
        return;
    }
    std::size_t rendered = 0;
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
        process_chunk({output.left.subspan(rendered, frames),
                       output.right.subspan(rendered, frames)}, song_position, true);
        rendered += static_cast<std::size_t>(frames);
        sample_position_ += frames;
        continuous_from_ = song_position + frames;
    }
    published_position_.store(sample_position_, std::memory_order_release);
}

} // namespace blokkily
