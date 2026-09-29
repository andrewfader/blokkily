// The program schema's song-wide rules (plan §F-E): consistency of every
// cross-reference, removing a track, and pruning unused audio files.

#include "blokkily/model/song.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace blokkily {

namespace {

bool refuse(std::string* why, std::string message) {
    if (why != nullptr) *why = std::move(message);
    return false;
}

bool finite_strip(const MixerStrip& strip) {
    return std::isfinite(strip.gain_db) && std::isfinite(strip.pan);
}

bool effect_slots_ok(const std::vector<EffectSlot>& slots, const std::string& where,
                     std::string* why) {
    for (const auto& slot : slots) {
        if (slot.plugin.format.empty())
            return refuse(why, where + ": an effect slot names no plugin format");
        for (std::size_t i = 0; i < slot.parameters.size(); ++i) {
            if (!std::isfinite(slot.parameters[i].value))
                return refuse(why, where + ": an effect parameter is not a finite number");
            for (std::size_t j = i + 1; j < slot.parameters.size(); ++j)
                if (slot.parameters[i].id == slot.parameters[j].id)
                    return refuse(why, where + ": an effect parameter is stored twice");
        }
    }
    return true;
}

// The insert chain a bus address names, or null when the bus does not exist.
const std::vector<EffectSlot>* chain_of(const Song& song, const ProcessorAddress& address) {
    switch (address.kind) {
    case BusKind::track:
        return address.bus < song.tracks.size() ? &song.tracks[address.bus].inserts : nullptr;
    case BusKind::ret:
        return address.bus < song.returns.size() ? &song.returns[address.bus].inserts : nullptr;
    case BusKind::master:
        return address.bus == 0 ? &song.master_inserts : nullptr;
    }
    return nullptr;
}

bool target_ok(const Song& song, const AutomationTarget& target, std::string* why) {
    const auto* chain = chain_of(song, target.processor);
    if (chain == nullptr)
        return refuse(why, "an automation lane targets a bus that does not exist");
    const auto slot = target.processor.slot;
    switch (target.kind) {
    case AutomationTarget::Kind::gain:
    case AutomationTarget::Kind::pan:
    case AutomationTarget::Kind::mute:
        if (slot != -1)
            return refuse(why, "a gain, pan or mute automation lane names a processor slot");
        return true;
    case AutomationTarget::Kind::parameter:
        if (target.parameter_index < 0)
            return refuse(why, "an automation lane has a negative parameter index");
        if (slot == -1 && target.processor.kind != BusKind::track)
            return refuse(why, "an automation lane targets the instrument of a bus that has none");
        if (slot < -1 || (slot >= 0 && static_cast<std::size_t>(slot) >= chain->size()))
            return refuse(why, "an automation lane targets an effect slot that does not exist");
        return true;
    }
    return refuse(why, "an automation lane has an unknown target kind");
}

// Two lanes drive the same thing when they name the same control; which
// control a gain or pan lane drives does not depend on the parameter fields.
bool same_control(const AutomationTarget& a, const AutomationTarget& b) {
    if (a.kind != b.kind || a.processor != b.processor) return false;
    return a.kind != AutomationTarget::Kind::parameter || a.parameter_index == b.parameter_index;
}

// Whether a processor address names a processor a parameter can belong to: a
// track's instrument or an insert that exists.
bool processor_exists(const Song& song, const ProcessorAddress& address) {
    const auto* chain = chain_of(song, address);
    if (chain == nullptr) return false;
    if (address.slot == -1) return address.kind == BusKind::track;
    return address.slot >= 0 && static_cast<std::size_t>(address.slot) < chain->size();
}

bool in_range(double value, double low, double high) {
    return std::isfinite(value) && value >= low && value <= high;
}

bool modulator_ok(const Song& song, const Modulator& modulator, std::string* why) {
    if (modulator.kind > Modulator::Kind::follower)
        return refuse(why, "a modulator has an unknown kind");
    if (modulator.shape > LfoShape::sample_and_hold)
        return refuse(why, "an LFO has an unknown shape");
    if (!in_range(modulator.rate_hz, Modulator::minimum_rate_hz, Modulator::maximum_rate_hz) ||
        !in_range(modulator.sync_beats, 0.0, Modulator::maximum_sync_beats))
        return refuse(why, "an LFO's rate is out of range");
    if (!in_range(modulator.value, 0.0, 1.0))
        return refuse(why, "a macro's value is outside 0..1");
    if (!in_range(modulator.attack_ms, Modulator::minimum_time_ms, Modulator::maximum_time_ms) ||
        !in_range(modulator.release_ms, Modulator::minimum_time_ms, Modulator::maximum_time_ms))
        return refuse(why, "a follower's attack or release is out of range");
    if (modulator.kind == Modulator::Kind::follower &&
        modulator.source_track >= song.tracks.size())
        return refuse(why, "a follower follows a track that does not exist");
    if (modulator.targets.size() > Modulator::maximum_targets)
        return refuse(why, "a modulator has too many targets");
    for (std::size_t i = 0; i < modulator.targets.size(); ++i) {
        const auto& target = modulator.targets[i];
        if (!processor_exists(song, target.processor))
            return refuse(why, "a modulation target names a processor that does not exist");
        if (target.parameter_index < 0)
            return refuse(why, "a modulation target has a negative parameter index");
        if (!in_range(target.depth, -1.0, 1.0))
            return refuse(why, "a modulation depth is outside -1..+1");
        for (std::size_t j = i + 1; j < modulator.targets.size(); ++j)
            if (modulator.targets[j].processor == target.processor &&
                modulator.targets[j].parameter_index == target.parameter_index)
                return refuse(why, "a modulator targets one parameter twice");
    }
    return true;
}

// Every insert's key names a track, a track's insert never keys from its own
// track, and no chain of keys leads back to where it started: the engine
// renders a key's source before the track it keys, which a loop forbids.
bool sidechains_ok(const Song& song, std::string* why) {
    const auto keys_ok = [&](const std::vector<EffectSlot>& slots) {
        for (const auto& slot : slots)
            if (slot.sidechain && *slot.sidechain >= song.tracks.size()) return false;
        return true;
    };
    for (const auto& bus : song.returns)
        if (!keys_ok(bus.inserts))
            return refuse(why, "a sidechain names a track that does not exist");
    if (!keys_ok(song.master_inserts))
        return refuse(why, "a sidechain names a track that does not exist");
    const auto count = song.tracks.size();
    std::vector<std::vector<std::size_t>> keyed_by(count);
    for (std::size_t t = 0; t < count; ++t) {
        if (!keys_ok(song.tracks[t].inserts))
            return refuse(why, "a sidechain names a track that does not exist");
        for (const auto& slot : song.tracks[t].inserts)
            if (slot.sidechain) {
                if (*slot.sidechain == t)
                    return refuse(why, "a track's insert is keyed from its own track");
                keyed_by[t].push_back(*slot.sidechain);
            }
    }
    // Depth-first, colouring each track while it is on the path.
    std::vector<int> colour(count, 0);
    const auto loops = [&](const auto& self, std::size_t track) -> bool {
        colour[track] = 1;
        for (const auto source : keyed_by[track]) {
            if (colour[source] == 1) return true;
            if (colour[source] == 0 && self(self, source)) return true;
        }
        colour[track] = 2;
        return false;
    };
    for (std::size_t t = 0; t < count; ++t)
        if (colour[t] == 0 && loops(loops, t))
            return refuse(why, "sidechain keys form a loop");
    return true;
}

bool input_ok(const TrackInput& input) {
    return input.source <= TrackInput::Source::midi_and_audio &&
           input.monitor <= TrackInput::Monitor::on && input.midi_channel >= -1 &&
           input.midi_channel <= 15 && (input.audio_channels == 1 || input.audio_channels == 2);
}

} // namespace

bool Song::consistent(std::string* why) const {
    if (patterns.empty()) return refuse(why, "the song has no pattern");
    if (tracks.empty()) return refuse(why, "the song has no track");
    if (!tempo.valid())
        return refuse(why, "the tempo map is not sorted from tick 0 or has a tempo out of range");
    if (!meter.valid())
        return refuse(why, "the meter map does not start at bar 0 or has an invalid meter");
    for (const auto& named : patterns)
        for (const auto& control : named.pattern.continuous())
            if (!Pattern::valid_continuous(control, named.pattern.length()))
                return refuse(why, "a controller event lies outside its pattern or range");
    for (const auto& clip : clips)
        if (clip.track >= tracks.size() || clip.pattern >= patterns.size() || clip.start < 0)
            return refuse(why, "a clip refers to a track or pattern that does not exist");
    for (const auto& clip : clips) {
        const Tick whole = patterns[clip.pattern].pattern.length() *
                           static_cast<Tick>(std::max<std::uint32_t>(1, clip.repeats));
        if (clip.length < 0 || clip.length > whole)
            return refuse(why, "a clip is cut outside the repeats it plays");
    }

    for (const auto& scene : launcher.scenes) {
        for (std::size_t t = 0; t < scene.cells.size(); ++t) {
            if (scene.cells[t].has_value()) {
                if (t >= tracks.size() || scene.cells[t]->pattern >= patterns.size())
                    return refuse(why, "a launcher slot refers to a track or pattern that does not exist");
            }
        }
    }

    for (const auto& file : audio_files)
        if (file.path.empty() || file.frames == 0 || file.sample_rate == 0 || file.channels == 0)
            return refuse(why, "an audio file has no path, frames, rate or channels");

    // Overlap is deliberately not checked: overlapping clips sum (decision 6).
    for (std::size_t i = 0; i < audio_clips.size(); ++i) {
        const auto& clip = audio_clips[i];
        if (clip.id == 0) return refuse(why, "an audio clip has no id");
        for (std::size_t j = i + 1; j < audio_clips.size(); ++j)
            if (audio_clips[j].id == clip.id) return refuse(why, "two audio clips share an id");
        if (clip.track >= tracks.size())
            return refuse(why, "an audio clip refers to a track that does not exist");
        if (clip.file >= audio_files.size())
            return refuse(why, "an audio clip refers to an audio file that does not exist");
        if (clip.start < 0) return refuse(why, "an audio clip starts before the song");
        if (clip.length_frames == 0) return refuse(why, "an audio clip is empty");
        const auto frames = audio_files[clip.file].frames;
        if (clip.offset_frames > frames || clip.length_frames > frames - clip.offset_frames)
            return refuse(why, "an audio clip runs past the end of its file");
        if (clip.fade_in_frames > clip.length_frames ||
            clip.fade_out_frames > clip.length_frames - clip.fade_in_frames)
            return refuse(why, "an audio clip's fades are longer than the clip");
        if (!std::isfinite(clip.gain_db))
            return refuse(why, "an audio clip's gain is not finite");
        if (!clip.warp.valid())
            return refuse(why, "an audio clip's warp is out of range");
    }

    for (std::size_t r = 0; r < returns.size(); ++r) {
        if (!finite_strip(returns[r].mix)) return refuse(why, "a return's strip is not finite");
        if (!effect_slots_ok(returns[r].inserts, "return " + std::to_string(r), why))
            return false;
    }
    if (!effect_slots_ok(master_inserts, "master", why)) return false;

    std::vector<const AutomationTarget*> targets;
    for (std::size_t t = 0; t < tracks.size(); ++t) {
        const auto& track = tracks[t];
        const auto where = "track " + std::to_string(t);
        if (!effect_slots_ok(track.inserts, where, why)) return false;
        for (std::size_t s = 0; s < track.sends.size(); ++s) {
            const auto& send = track.sends[s];
            if (send.bus >= returns.size())
                return refuse(why, where + ": a send refers to a return that does not exist");
            if (!std::isfinite(send.level_db))
                return refuse(why, where + ": a send level is not finite");
            for (std::size_t other = s + 1; other < track.sends.size(); ++other)
                if (track.sends[other].bus == send.bus)
                    return refuse(why, where + ": two sends feed the same return");
        }
        if (!input_ok(track.input)) return refuse(why, where + ": the input is out of range");
        if (track.automation_mode > AutomationMode::write)
            return refuse(why, where + ": unknown automation mode");
        for (const auto& lane : track.automation) {
            if (!lane.well_formed())
                return refuse(why, where + ": an automation lane is out of order or not finite");
            if (!target_ok(*this, lane.target, why)) return false;
            if (lane.target.kind == AutomationTarget::Kind::pan)
                for (const auto& point : lane.points)
                    if (point.value < -1.0 || point.value > 1.0)
                        return refuse(why, where + ": a pan automation point is outside -1..+1");
            if (lane.target.kind == AutomationTarget::Kind::mute)
                for (const auto& point : lane.points)
                    if (point.value < 0.0 || point.value > 1.0)
                        return refuse(why, where + ": a mute automation point is outside 0..1");
            for (const auto* seen : targets)
                if (same_control(*seen, lane.target))
                    return refuse(why, "two automation lanes drive the same control");
            targets.push_back(&lane.target);
        }
    }
    if (!sidechains_ok(*this, why)) return false;
    if (modulators.size() > maximum_modulators)
        return refuse(why, "the song has too many modulators");
    for (const auto& modulator : modulators)
        if (!modulator_ok(*this, modulator, why)) return false;
    return true;
}

std::vector<std::optional<std::size_t>> Song::remove_track(std::size_t index) {
    if (index >= tracks.size() || tracks.size() < 2) return {};
    std::vector<std::optional<std::size_t>> map(tracks.size());
    for (std::size_t old = 0; old < tracks.size(); ++old)
        if (old != index) map[old] = old < index ? old : old - 1;
    const auto moved = [&map](std::size_t old) {
        return old < map.size() && map[old] ? *map[old] : old;
    };

    tracks.erase(tracks.begin() + static_cast<std::ptrdiff_t>(index));
    launcher.remove_track(index);
    std::erase_if(clips, [index](const Clip& clip) { return clip.track == index; });
    for (auto& clip : clips) clip.track = moved(clip.track);
    std::erase_if(audio_clips, [index](const AudioClip& clip) { return clip.track == index; });
    for (auto& clip : audio_clips) clip.track = moved(clip.track);

    // Sends travel with the track that owns them and name returns, not tracks,
    // so of what remains only automation lanes can name a track by index.
    for (auto& track : tracks) {
        std::erase_if(track.automation, [index](const AutomationLane& lane) {
            return lane.target.processor.kind == BusKind::track &&
                   lane.target.processor.bus == index;
        });
        for (auto& lane : track.automation) {
            auto& address = lane.target.processor;
            if (address.kind == BusKind::track)
                address.bus = static_cast<std::uint32_t>(moved(address.bus));
        }
    }
    // A key from the removed track is let go; keys from the others follow
    // their tracks.
    const auto rekey = [&](std::vector<EffectSlot>& slots) {
        for (auto& slot : slots) {
            if (!slot.sidechain) continue;
            if (*slot.sidechain == index) slot.sidechain.reset();
            else slot.sidechain = static_cast<std::uint32_t>(moved(*slot.sidechain));
        }
    };
    for (auto& track : tracks) rekey(track.inserts);
    for (auto& bus : returns) rekey(bus.inserts);
    rekey(master_inserts);
    // A follower of the removed track has nothing left to follow; targets on
    // its processors go with it.
    std::erase_if(modulators, [index](const Modulator& modulator) {
        return modulator.kind == Modulator::Kind::follower && modulator.source_track == index;
    });
    for (auto& modulator : modulators) {
        if (modulator.kind == Modulator::Kind::follower)
            modulator.source_track = static_cast<std::uint32_t>(moved(modulator.source_track));
        std::erase_if(modulator.targets, [index](const ModulationTarget& target) {
            return target.processor.kind == BusKind::track && target.processor.bus == index;
        });
        for (auto& target : modulator.targets)
            if (target.processor.kind == BusKind::track)
                target.processor.bus = static_cast<std::uint32_t>(moved(target.processor.bus));
    }
    return map;
}

namespace {
// Drops what drives the processor at `removed` and moves what drives the
// processors after it on the same bus down one slot, for every automation
// lane and modulation target; `on_bus` says whether an address is on the
// bus concerned at all.
template <typename OnBus>
void forget_processor(Song& song, OnBus on_bus, std::int32_t removed) {
    const auto gone = [&](const ProcessorAddress& address) {
        return on_bus(address) && address.slot == removed;
    };
    const auto follow = [&](ProcessorAddress& address) {
        if (on_bus(address) && address.slot > removed) --address.slot;
    };
    for (auto& track : song.tracks) {
        std::erase_if(track.automation,
                      [&](const AutomationLane& lane) { return gone(lane.target.processor); });
        for (auto& lane : track.automation) follow(lane.target.processor);
    }
    for (auto& modulator : song.modulators) {
        std::erase_if(modulator.targets,
                      [&](const ModulationTarget& target) { return gone(target.processor); });
        for (auto& target : modulator.targets) follow(target.processor);
    }
}
} // namespace

bool Song::remove_insert(BusKind kind, std::size_t bus, std::size_t slot) {
    std::vector<EffectSlot>* chain = nullptr;
    switch (kind) {
    case BusKind::track: chain = bus < tracks.size() ? &tracks[bus].inserts : nullptr; break;
    case BusKind::ret: chain = bus < returns.size() ? &returns[bus].inserts : nullptr; break;
    case BusKind::master: chain = bus == 0 ? &master_inserts : nullptr; break;
    }
    if (chain == nullptr || slot >= chain->size()) return false;
    chain->erase(chain->begin() + static_cast<std::ptrdiff_t>(slot));
    forget_processor(
        *this,
        [kind, bus](const ProcessorAddress& address) {
            return address.kind == kind && address.bus == bus;
        },
        static_cast<std::int32_t>(slot));
    return true;
}

bool Song::remove_return(std::size_t bus) {
    if (bus >= returns.size()) return false;
    returns.erase(returns.begin() + static_cast<std::ptrdiff_t>(bus));
    for (auto& track : tracks) {
        std::erase_if(track.sends, [bus](const Send& send) { return send.bus == bus; });
        for (auto& send : track.sends)
            if (send.bus > bus) --send.bus;
    }
    const auto on_return = [bus](const ProcessorAddress& address) {
        return address.kind == BusKind::ret && address.bus == bus;
    };
    const auto later = [bus](ProcessorAddress& address) {
        if (address.kind == BusKind::ret && address.bus > bus) --address.bus;
    };
    for (auto& track : tracks) {
        std::erase_if(track.automation,
                      [&](const AutomationLane& lane) { return on_return(lane.target.processor); });
        for (auto& lane : track.automation) later(lane.target.processor);
    }
    for (auto& modulator : modulators) {
        std::erase_if(modulator.targets,
                      [&](const ModulationTarget& target) { return on_return(target.processor); });
        for (auto& target : modulator.targets) later(target.processor);
    }
    return true;
}

std::vector<std::optional<std::size_t>> Song::prune_audio_files() {
    std::vector<bool> used(audio_files.size(), false);
    for (const auto& clip : audio_clips)
        if (clip.file < used.size()) used[clip.file] = true;
    std::vector<std::optional<std::size_t>> map(audio_files.size());
    std::vector<AudioFileRef> kept;
    for (std::size_t old = 0; old < audio_files.size(); ++old) {
        if (!used[old]) continue;
        map[old] = kept.size();
        kept.push_back(std::move(audio_files[old]));
    }
    audio_files = std::move(kept);
    for (auto& clip : audio_clips)
        if (clip.file < map.size() && map[clip.file]) clip.file = *map[clip.file];
    return map;
}

AudioClipId Song::next_audio_clip_id() const {
    AudioClipId highest = 0;
    for (const auto& clip : audio_clips) highest = std::max(highest, clip.id);
    return highest + 1;
}

} // namespace blokkily
