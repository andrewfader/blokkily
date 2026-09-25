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
        if (slot != -1)
            return refuse(why, "a gain or pan automation lane names a processor slot");
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

bool input_ok(const TrackInput& input) {
    return input.source <= TrackInput::Source::midi_and_audio &&
           input.monitor <= TrackInput::Monitor::on && input.midi_channel >= -1 &&
           input.midi_channel <= 15 && (input.audio_channels == 1 || input.audio_channels == 2);
}

} // namespace

bool Song::consistent(std::string* why) const {
    if (patterns.empty()) return refuse(why, "the song has no pattern");
    if (tracks.empty()) return refuse(why, "the song has no track");
    for (const auto& clip : clips)
        if (clip.track >= tracks.size() || clip.pattern >= patterns.size() || clip.start < 0)
            return refuse(why, "a clip refers to a track or pattern that does not exist");

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
            for (const auto* seen : targets)
                if (same_control(*seen, lane.target))
                    return refuse(why, "two automation lanes drive the same control");
            targets.push_back(&lane.target);
        }
    }
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
    return map;
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
