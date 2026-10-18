#include "blokkily/project/dawproject.hpp"

#include "blokkily/audio/mixer.hpp"
#include "blokkily/model/number_text.hpp"

#include <juce_core/juce_core.h>
#include <pugixml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace blokkily {
namespace {

bool fail(std::string* error, std::string message) {
    if (error != nullptr) *error = std::move(message);
    return false;
}

// Numbers are written the shortest way that reads back exactly, with a point
// whatever the machine's locale.
std::string number(double value) {
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), result.ptr);
}

std::nullopt_t nothing(std::string* error, std::string message) {
    (void)fail(error, std::move(message));
    return std::nullopt;
}

// A float (a velocity, an expression) the shortest way that reads back as
// that float: 0.6, not 0.60000002384185791.
std::string number(float value) {
    std::array<char, 32> buffer{};
    const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), result.ptr);
}

const char* boolean(bool value) { return value ? "true" : "false"; }

using AddressKey = std::tuple<int, std::uint32_t, std::int32_t>;
AddressKey key_of(ProcessorAddress where) {
    return {static_cast<int>(where.kind), where.bus, where.slot};
}

std::string bus_label(const Song& song, ProcessorAddress where) {
    switch (where.kind) {
    case BusKind::track:
        return where.bus < song.tracks.size() ? "Track \"" + song.tracks[where.bus].name + "\""
                                              : "a track";
    case BusKind::ret:
        return where.bus < song.returns.size() ? "Return \"" + song.returns[where.bus].name + "\""
                                               : "a return";
    case BusKind::master: return "The master";
    }
    return "A bus";
}

// The ids a channel's own parameters are referred to by.
struct ChannelIds {
    std::string channel, volume, pan, mute;
};

// Which channel parameter an automation lane moves.
enum class StripParameter { volume, pan, mute };

// A stamp for every entry, so that writing one song twice gives one archive.
juce::Time archive_time() { return juce::Time(2020, 0, 1, 0, 0, 0, 0, true); }

class Writer {
public:
    Writer(const Song& song, const DawProjectOptions& options)
        : song_(song), options_(options), tpb_(std::max<Tick>(1, song.ticks_per_beat())) {}

    bool write(const std::filesystem::path& path, std::string* error);
    DawProjectReport report;

private:
    std::string id() { return "id" + std::to_string(next_id_++); }
    double beats(double ticks) const { return ticks / static_cast<double>(tpb_); }
    void omit(std::string what) { report.omitted.push_back(std::move(what)); }

    const PortableState* portable(ProcessorAddress where) const {
        for (const auto& [address, state] : options_.states)
            if (address == where) return &state;
        return nullptr;
    }
    const ChannelIds* channel_of(ProcessorAddress where) const;

    void collect_parameters();
    void write_transport(pugi::xml_node project);
    void write_channel(pugi::xml_node track, const ChannelIds& ids, const char* role,
                       const MixerStrip& strip, double gain_db, const std::string& destination);
    void write_device(pugi::xml_node devices, ProcessorAddress where, const PluginSlot& slot,
                      bool bypass, const std::vector<EffectParameter>* settings);
    void write_parameters(pugi::xml_node device, ProcessorAddress where,
                          const PortableState* state,
                          const std::vector<EffectParameter>* settings);
    void write_clips(pugi::xml_node clips, std::size_t track);
    void write_audio_clips(pugi::xml_node clips, std::size_t track);
    void write_note(pugi::xml_node notes, const ScheduledNote& note, Tick clip_time,
                    const std::vector<const ScheduledExpression*>& expression);
    void write_automation(pugi::xml_node lanes, std::size_t track);
    pugi::xml_node points(pugi::xml_node parent, const char* name = "Points");

    const Song& song_;
    const DawProjectOptions& options_;
    const Tick tpb_;
    int next_id_ = 0;
    std::vector<ScheduledEvents> arranged_;
    std::vector<ChannelIds> tracks_, returns_;
    ChannelIds master_;
    std::string tempo_id_, meter_id_;
    // The parameters automation and step locks move, by processor, and the
    // id each is written under once its device is.
    std::map<AddressKey, std::set<std::int32_t>> used_parameters_;
    std::map<std::pair<AddressKey, std::int32_t>, std::string> parameter_ids_;
    // What the archive holds beside project.xml and metadata.xml.
    std::vector<std::pair<std::string, std::vector<std::byte>>> blobs_;
    std::vector<std::pair<std::string, std::filesystem::path>> files_;
    std::map<std::size_t, std::string> audio_paths_;
};

const ChannelIds* Writer::channel_of(ProcessorAddress where) const {
    switch (where.kind) {
    case BusKind::track: return where.bus < tracks_.size() ? &tracks_[where.bus] : nullptr;
    case BusKind::ret: return where.bus < returns_.size() ? &returns_[where.bus] : nullptr;
    case BusKind::master: return &master_;
    }
    return nullptr;
}

pugi::xml_node Writer::points(pugi::xml_node parent, const char* name) {
    auto node = parent.append_child(name);
    node.append_attribute("id") = id().c_str();
    return node;
}

void Writer::collect_parameters() {
    for (std::size_t track = 0; track < song_.tracks.size(); ++track) {
        for (const auto& lane : song_.tracks[track].automation)
            if (lane.target.kind == AutomationTarget::Kind::parameter)
                used_parameters_[key_of(lane.target.processor)].insert(lane.target.parameter_index);
        for (const auto& lock : arranged_[track].parameters) {
            if (lock.kind != ParameterLock::Kind::automation) continue;
            const ProcessorAddress where{lock.bus_kind,
                                         lock.bus_kind == BusKind::track
                                             ? static_cast<std::uint32_t>(track)
                                             : lock.bus,
                                         lock.slot};
            used_parameters_[key_of(where)].insert(lock.index);
        }
    }
}

void Writer::write_transport(pugi::xml_node project) {
    auto transport = project.append_child("Transport");
    auto tempo = transport.append_child("Tempo");
    tempo_id_ = id();
    tempo.append_attribute("id") = tempo_id_.c_str();
    tempo.append_attribute("name") = "Tempo";
    tempo.append_attribute("unit") = "bpm";
    tempo.append_attribute("min") = number(minimum_bpm).c_str();
    tempo.append_attribute("max") = number(maximum_bpm).c_str();
    tempo.append_attribute("value") = number(song_.tempo.points.front().bpm).c_str();
    auto meter = transport.append_child("TimeSignature");
    meter_id_ = id();
    meter.append_attribute("id") = meter_id_.c_str();
    meter.append_attribute("numerator") = song_.meter.changes.front().numerator;
    meter.append_attribute("denominator") = song_.meter.changes.front().denominator;
}

void Writer::write_parameters(pugi::xml_node device, ProcessorAddress where,
                              const PortableState* state,
                              const std::vector<EffectParameter>* settings) {
    auto parameters = device.append_child("Parameters");
    const auto used = used_parameters_.find(key_of(where));
    std::set<std::int32_t> listed;
    if (used != used_parameters_.end()) listed = used->second;
    // A built-in effect keeps its settings in the song rather than in a
    // state, so every one of them is written.
    if (settings != nullptr)
        for (const auto& setting : *settings) listed.insert(setting.id);
    for (const auto index : listed) {
        auto parameter = parameters.append_child("RealParameter");
        const auto parameter_id = id();
        parameter_ids_[{key_of(where), index}] = parameter_id;
        parameter.append_attribute("id") = parameter_id.c_str();
        std::string name = "Parameter " + std::to_string(index);
        double min = 0.0, max = 1.0, value = 0.0;
        bool ranged = false;
        std::optional<std::int64_t> portable_id;
        if (state != nullptr)
            for (const auto& known : state->parameters)
                if (known.id == index) {
                    name = known.name;
                    min = known.min;
                    max = known.max;
                    value = known.value;
                    ranged = true;
                    portable_id = known.portable_id;
                }
        if (settings != nullptr)
            for (const auto& setting : *settings)
                if (setting.id == index) value = setting.value;
        parameter.append_attribute("name") = name.c_str();
        if (portable_id) parameter.append_attribute("parameterID") = static_cast<long long>(*portable_id);
        parameter.append_attribute("unit") = "linear";
        if (ranged) {
            parameter.append_attribute("min") = number(min).c_str();
            parameter.append_attribute("max") = number(max).c_str();
        }
        parameter.append_attribute("value") = number(value).c_str();
    }
}

} // namespace

std::vector<std::byte> dawproject_slot_bytes(const PluginSlot& slot) {
    const std::string head = "BLOKKILY-SLOT 1\n" + slot.format + "\n" + slot.path + "\n" +
                             slot.identifier + "\n";
    std::vector<std::byte> bytes(head.size());
    std::memcpy(bytes.data(), head.data(), head.size());
    const auto state = slot.state.bytes();
    bytes.insert(bytes.end(), state.begin(), state.end());
    return bytes;
}

std::optional<PluginSlot> dawproject_slot(std::span<const std::byte> bytes) {
    std::string_view text(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::array<std::string, 4> lines;
    std::size_t at = 0;
    for (auto& line : lines) {
        const auto end = text.find('\n', at);
        if (end == std::string_view::npos) return std::nullopt;
        line = std::string(text.substr(at, end - at));
        at = end + 1;
    }
    if (lines[0] != "BLOKKILY-SLOT 1") return std::nullopt;
    PluginSlot slot;
    slot.format = lines[1];
    slot.path = lines[2];
    slot.identifier = lines[3];
    slot.state = std::vector<std::byte>(bytes.begin() + static_cast<std::ptrdiff_t>(at), bytes.end());
    return slot;
}

namespace {

void Writer::write_device(pugi::xml_node devices, ProcessorAddress where, const PluginSlot& slot,
                          bool bypass, const std::vector<EffectParameter>* settings) {
    const auto* state = portable(where);
    const bool clap = slot.format == "CLAP";
    const bool vst3 = slot.format == "VST3";
    const bool builtin = settings != nullptr && slot.format == "Built-in";
    const char* element = clap ? "ClapPlugin" : vst3 ? "Vst3Plugin" : builtin ? "BuiltinDevice" : "Device";
    auto device = devices.append_child(element);
    device.append_attribute("id") = id().c_str();
    std::string name = state != nullptr ? state->device_name : "";
    if (name.empty()) name = !slot.identifier.empty() ? slot.identifier
                                                      : std::filesystem::path(slot.path).stem().string();
    device.append_attribute("name") = name.c_str();
    std::string device_id = state != nullptr ? state->device_id : clap ? slot.identifier : "";
    if (!device_id.empty()) device.append_attribute("deviceID") = device_id.c_str();
    device.append_attribute("deviceName") = name.c_str();
    device.append_attribute("deviceRole") = where.instrument() ? "instrument" : "audioFX";
    if (state != nullptr && !state->device_vendor.empty())
        device.append_attribute("deviceVendor") = state->device_vendor.c_str();
    device.append_attribute("loaded") = "true";
    write_parameters(device, where, state, settings);
    auto enabled = device.append_child("Enabled");
    enabled.append_attribute("id") = id().c_str();
    enabled.append_attribute("name") = "On/Off";
    enabled.append_attribute("value") = boolean(!bypass);

    const auto where_text = bus_label(song_, where) + (where.instrument() ? "'s instrument "
                                                                             : "'s insert ") +
                            name;
    if ((clap || vst3) && state != nullptr && !state->preset.empty()) {
        const auto file = "plugins/" + std::to_string(blobs_.size() + 1) + "." + state->preset_extension;
        blobs_.emplace_back(file, state->preset);
        device.append_child("State").append_attribute("path") = file.c_str();
        ++report.plugin_states;
    } else if (clap || vst3) {
        omit(where_text + ": its state was not captured (load the plugin before exporting)");
    } else if (builtin) {
        omit(where_text + ": a built-in effect, written as a generic device other hosts do not load");
    } else {
        omit(where_text + ": " + slot.format + " has no DAWproject device; written as a placeholder");
        // What the placeholder stands for, so Blokkily opening the file loads
        // it again (dawproject_slot_bytes); other hosts ignore it.
        const auto file = "plugins/" + std::to_string(blobs_.size() + 1) + ".blokkily-slot";
        blobs_.emplace_back(file, dawproject_slot_bytes(slot));
        device.append_child("State").append_attribute("path") = file.c_str();
    }
}

void Writer::write_channel(pugi::xml_node track, const ChannelIds& ids, const char* role,
                           const MixerStrip& strip, double gain_db, const std::string& destination) {
    auto channel = track.append_child("Channel");
    channel.append_attribute("id") = ids.channel.c_str();
    channel.append_attribute("role") = role;
    channel.append_attribute("audioChannels") = 2;
    if (!destination.empty()) channel.append_attribute("destination") = destination.c_str();
    channel.append_attribute("solo") = boolean(strip.solo);
    // Devices go first; the caller fills them in.
    channel.append_child("Devices");
    auto mute = channel.append_child("Mute");
    mute.append_attribute("id") = ids.mute.c_str();
    mute.append_attribute("name") = "Mute";
    mute.append_attribute("value") = boolean(strip.mute);
    auto pan = channel.append_child("Pan");
    pan.append_attribute("id") = ids.pan.c_str();
    pan.append_attribute("name") = "Pan";
    pan.append_attribute("unit") = "normalized";
    pan.append_attribute("min") = "0";
    pan.append_attribute("max") = "1";
    pan.append_attribute("value") = number((std::clamp(strip.pan, -1.0, 1.0) + 1.0) / 2.0).c_str();
    channel.append_child("Sends");
    const double linear = db_to_linear(gain_db);
    auto volume = channel.append_child("Volume");
    volume.append_attribute("id") = ids.volume.c_str();
    volume.append_attribute("name") = "Volume";
    volume.append_attribute("unit") = "linear";
    volume.append_attribute("min") = "0";
    volume.append_attribute("max") = number(std::max(2.0, linear)).c_str();
    volume.append_attribute("value") = number(linear).c_str();
}

void Writer::write_note(pugi::xml_node notes, const ScheduledNote& note, Tick clip_time,
                        const std::vector<const ScheduledExpression*>& expression) {
    auto element = notes.append_child("Note");
    element.append_attribute("time") = number(beats(static_cast<double>(note.start - clip_time))).c_str();
    element.append_attribute("duration") = number(beats(static_cast<double>(note.duration))).c_str();
    element.append_attribute("channel") = 0;
    element.append_attribute("key") = note.key;
    element.append_attribute("vel") = number(std::clamp(note.velocity, 0.0F, 1.0F)).c_str();
    ++report.notes;

    // A retune and the pitch expression are one transpose lane, in semitones
    // from the key; timbre and pressure keep lanes of their own.
    struct Lane {
        const char* expression;
        const char* unit;
        std::vector<std::pair<Tick, double>> points;
    };
    std::array<Lane, 3> lanes{Lane{"transpose", "semitones", {}}, Lane{"timbre", "normalized", {}},
                              Lane{"pressure", "normalized", {}}};
    const double retune = note.cents / 100.0;
    if (note.cents != 0.0) lanes[0].points.emplace_back(0, retune);
    for (const auto* value : expression) {
        const Tick at = value->start - note.start;
        switch (value->kind) {
        case NoteExpression::Kind::pitch: lanes[0].points.emplace_back(at, retune + value->value); break;
        case NoteExpression::Kind::timbre: lanes[1].points.emplace_back(at, static_cast<double>(value->value)); break;
        case NoteExpression::Kind::pressure: lanes[2].points.emplace_back(at, static_cast<double>(value->value)); break;
        }
    }
    const auto used = std::count_if(lanes.begin(), lanes.end(),
                                    [](const Lane& lane) { return !lane.points.empty(); });
    if (used == 0) return;
    // A note holds one timeline; several lanes go inside a Lanes.
    auto holder = used == 1 ? element : element.append_child("Lanes");
    for (const auto& lane : lanes) {
        if (lane.points.empty()) continue;
        auto node = holder.append_child("Points");
        node.append_attribute("unit") = lane.unit;
        node.append_child("Target").append_attribute("expression") = lane.expression;
        for (const auto& [at, value] : lane.points) {
            auto point = node.append_child("RealPoint");
            point.append_attribute("time") = number(beats(static_cast<double>(at))).c_str();
            point.append_attribute("value") = number(value).c_str();
            point.append_attribute("interpolation") = "hold";
            ++report.automation_points;
        }
    }
}

void Writer::write_clips(pugi::xml_node clips, std::size_t track) {
    struct Placement {
        Tick start = 0;
        Tick end = 0;
        std::string name;
        std::vector<const ScheduledNote*> notes;
        std::vector<const ScheduledContinuous*> controls;
        std::vector<const ScheduledParameter*> locks;
    };
    std::vector<Placement> placements;
    for (const auto& clip : song_.clips) {
        // A MIDI take kept but not played is not heard, so not exported.
        if (clip.track != track || clip.pattern >= song_.patterns.size() || clip.comped_out) continue;
        const auto& slot = song_.patterns[clip.pattern];
        Placement placement;
        placement.start = clip.start;
        placement.end = clip.start + clip.span(slot.pattern.length());
        placement.name = song_.sections_whole() && slot.section < song_.sections.size()
                             ? song_.sections[slot.section].name
                             : slot.name;
        if (placement.end > placement.start) placements.push_back(std::move(placement));
    }
    std::stable_sort(placements.begin(), placements.end(),
                     [](const Placement& a, const Placement& b) { return a.start < b.start; });
    if (placements.empty()) return;
    // Each event goes to the placement it starts in: the last one that has
    // begun by then, or the first for one played ahead of it.
    const auto owner = [&](Tick at) -> Placement& {
        auto found = std::upper_bound(placements.begin(), placements.end(), at,
                                      [](Tick tick, const Placement& p) { return tick < p.start; });
        return found == placements.begin() ? placements.front() : *std::prev(found);
    };
    const auto& events = arranged_[track];
    for (const auto& note : events.notes) {
        if (note.key < 0 || note.key > 127 || note.duration <= 0) continue;
        owner(note.start).notes.push_back(&note);
    }
    for (const auto& control : events.continuous) owner(control.start).controls.push_back(&control);
    std::size_t modulation_locks = 0;
    for (const auto& lock : events.parameters) {
        if (lock.kind == ParameterLock::Kind::modulation) {
            ++modulation_locks;
            continue;
        }
        owner(lock.start).locks.push_back(&lock);
    }
    if (modulation_locks > 0)
        omit("Track \"" + song_.tracks[track].name + "\": " + std::to_string(modulation_locks) +
             " modulation step locks (DAWproject has no parameter offsets)");

    // Per-note expression is addressed to its note by key and retune: it
    // belongs to the latest such note sounding when it arrives.
    std::map<const ScheduledNote*, std::vector<const ScheduledExpression*>> expression;
    for (const auto& value : events.expressions) {
        const ScheduledNote* holder = nullptr;
        for (const auto& note : events.notes)
            if (note.key == value.key && std::abs(note.cents - value.cents) < 1e-9 &&
                note.start <= value.start && value.start < note.start + note.duration &&
                (holder == nullptr || note.start >= holder->start))
                holder = &note;
        if (holder != nullptr) expression[holder].push_back(&value);
    }

    for (std::size_t index = 0; index < placements.size(); ++index) {
        const auto& placement = placements[index];
        Tick from = placement.start;
        Tick to = placement.end;
        for (const auto* note : placement.notes) {
            from = std::min(from, note->start);
            to = std::max(to, note->start + note->duration);
        }
        for (const auto* control : placement.controls) from = std::min(from, control->start);
        for (const auto* lock : placement.locks) from = std::min(from, lock->start);
        // A note held past the placement keeps its length, up to where the
        // track's next clip begins.
        if (index + 1 < placements.size() && to > placements[index + 1].start)
            to = std::max(placement.end, placements[index + 1].start);
        auto clip = clips.append_child("Clip");
        clip.append_attribute("name") = placement.name.c_str();
        clip.append_attribute("time") = number(beats(static_cast<double>(from))).c_str();
        clip.append_attribute("duration") = number(beats(static_cast<double>(to - from))).c_str();
        clip.append_attribute("playStart") = "0";
        ++report.clips;

        const bool timelines = !placement.controls.empty() || !placement.locks.empty();
        auto content = timelines ? clip.append_child("Lanes") : clip;
        if (timelines) content.append_attribute("id") = id().c_str();
        auto notes = content.append_child("Notes");
        notes.append_attribute("id") = id().c_str();
        static const std::vector<const ScheduledExpression*> none;
        for (const auto* note : placement.notes) {
            const auto found = expression.find(note);
            write_note(notes, *note, from, found != expression.end() ? found->second : none);
        }

        // Controller movements, one lane per controller, as MIDI values
        // scaled to 0..1 (the pitch wheel centred at 0.5).
        std::map<std::tuple<int, int>, std::vector<const ScheduledContinuous*>> by_controller;
        for (const auto* control : placement.controls)
            by_controller[{static_cast<int>(control->event.kind), control->event.controller}].push_back(control);
        for (const auto& [which, moves] : by_controller) {
            const auto kind = static_cast<ContinuousEvent::Kind>(std::get<0>(which));
            auto node = points(content);
            node.append_attribute("unit") = "normalized";
            auto target = node.append_child("Target");
            switch (kind) {
            case ContinuousEvent::Kind::pitch_bend: target.append_attribute("expression") = "pitchBend"; break;
            case ContinuousEvent::Kind::control_change:
                target.append_attribute("expression") = "channelController";
                target.append_attribute("controller") = std::get<1>(which);
                break;
            case ContinuousEvent::Kind::channel_pressure:
                target.append_attribute("expression") = "channelPressure";
                break;
            case ContinuousEvent::Kind::poly_pressure:
                target.append_attribute("expression") = "polyPressure";
                target.append_attribute("key") = std::get<1>(which);
                break;
            }
            target.append_attribute("channel") = 0;
            const double full = kind == ContinuousEvent::Kind::pitch_bend ? 16383.0 : 127.0;
            for (const auto* move : moves) {
                auto point = node.append_child("RealPoint");
                point.append_attribute("time") = number(beats(static_cast<double>(move->start - from))).c_str();
                point.append_attribute("value") = number(move->event.value / full).c_str();
                point.append_attribute("interpolation") = "hold";
                ++report.automation_points;
            }
        }

        // Value step locks hold from their step on: hold points on the
        // parameter, in the plugin's own units.
        std::map<std::pair<AddressKey, std::int32_t>, std::vector<const ScheduledParameter*>> by_parameter;
        for (const auto* lock : placement.locks) {
            const ProcessorAddress where{lock->bus_kind,
                                         lock->bus_kind == BusKind::track
                                             ? static_cast<std::uint32_t>(track)
                                             : lock->bus,
                                         lock->slot};
            by_parameter[{key_of(where), lock->index}].push_back(lock);
        }
        for (const auto& [which, locks] : by_parameter) {
            const auto found = parameter_ids_.find(which);
            if (found == parameter_ids_.end()) continue;
            auto node = points(content);
            node.append_attribute("unit") = "linear";
            node.append_child("Target").append_attribute("parameter") = found->second.c_str();
            for (const auto* lock : locks) {
                auto point = node.append_child("RealPoint");
                point.append_attribute("time") = number(beats(static_cast<double>(lock->start - from))).c_str();
                point.append_attribute("value") = number(lock->value).c_str();
                point.append_attribute("interpolation") = "hold";
                ++report.automation_points;
            }
        }
    }
}

void Writer::write_audio_clips(pugi::xml_node clips, std::size_t track) {
    for (const auto& clip : song_.audio_clips) {
        if (clip.track != track) continue;
        // A frozen track's render is not exported: its instrument and notes
        // are, which is what the render was made from.
        if (clip.freeze) continue;
        if (clip.file >= song_.audio_files.size()) continue;
        const auto& file = song_.audio_files[clip.file];
        const auto clip_name = file.path.stem().string();
        const auto where = "Track \"" + song_.tracks[track].name + "\"'s audio clip " + clip_name;
        std::error_code missing;
        if (file.sample_rate == 0 || !std::filesystem::is_regular_file(file.path, missing)) {
            omit(where + ": its file " + file.path.string() + " was not found");
            continue;
        }
        if (clip.gain_db != 0.0) omit(where + ": its clip gain (DAWproject clips have none)");
        if (clip.warp.semitones != 0 || clip.warp.cents != 0.0)
            omit(where + ": its pitch shift (DAWproject warps keep pitch)");

        auto archived = audio_paths_.find(clip.file);
        if (archived == audio_paths_.end()) {
            // One archive entry per file, named after it, never two alike.
            std::string name = file.path.filename().string();
            for (int copy = 2; std::any_of(files_.begin(), files_.end(),
                                           [&](const auto& entry) { return entry.first == "audio/" + name; });
                 ++copy)
                name = file.path.stem().string() + " " + std::to_string(copy) + file.path.extension().string();
            files_.emplace_back("audio/" + name, file.path);
            archived = audio_paths_.emplace(clip.file, "audio/" + name).first;
            ++report.audio_files;
        }

        const double rate = static_cast<double>(file.sample_rate);
        const double offset = static_cast<double>(clip.offset_frames) / rate;
        const double length = static_cast<double>(clip.length_frames) / rate;
        const double start = static_cast<double>(clip.start);
        const double end = audio_clip_tick_at(song_, clip, length);
        auto element = clips.append_child("Clip");
        element.append_attribute("name") = clip_name.c_str();
        element.append_attribute("time") = number(beats(start)).c_str();
        element.append_attribute("duration") = number(beats(end - start)).c_str();
        element.append_attribute("playStart") = "0";
        element.append_attribute("fadeTimeUnit") = "seconds";
        element.append_attribute("fadeInTime") = number(static_cast<double>(clip.fade_in_frames) / rate).c_str();
        element.append_attribute("fadeOutTime") = number(static_cast<double>(clip.fade_out_frames) / rate).c_str();
        if (clip.comped_out) element.append_attribute("enable") = "false";
        ++report.clips;

        // Where the audio sounds is the one rule the engine plays it by
        // (audio_clip_tick_at), warped or not: written as warp markers at
        // the clip's ends, every tempo point and every beat between.
        auto warps = element.append_child("Warps");
        warps.append_attribute("id") = id().c_str();
        warps.append_attribute("timeUnit") = "beats";
        warps.append_attribute("contentTimeUnit") = "seconds";
        auto audio = warps.append_child("Audio");
        audio.append_attribute("id") = id().c_str();
        audio.append_attribute("algorithm") = clip.warp.active() ? "stretch" : "repitch";
        audio.append_attribute("channels") = file.channels;
        audio.append_attribute("sampleRate") = file.sample_rate;
        audio.append_attribute("duration") = number(static_cast<double>(file.frames) / rate).c_str();
        audio.append_child("File").append_attribute("path") = archived->second.c_str();
        std::set<double> ticks{start, end};
        for (const auto& point : song_.tempo.points)
            if (point.at > start && point.at < end) ticks.insert(static_cast<double>(point.at));
        for (double beat = std::ceil(start / static_cast<double>(tpb_)) * static_cast<double>(tpb_);
             beat < end; beat += static_cast<double>(tpb_))
            if (beat > start) ticks.insert(beat);
        for (const double at : ticks) {
            auto warp = warps.append_child("Warp");
            warp.append_attribute("time") = number(beats(at - start)).c_str();
            const double seconds = at == start ? 0.0 : at == end ? length : audio_clip_seconds_at(song_, clip, at);
            warp.append_attribute("contentTime") = number(offset + seconds).c_str();
        }
    }
}

void Writer::write_automation(pugi::xml_node lanes, std::size_t track) {
    const auto& owner = song_.tracks[track];
    if (owner.automation_mode == AutomationMode::off) {
        if (!owner.automation.empty())
            omit("Track \"" + owner.name + "\": its automation, switched off (written only when it plays)");
        return;
    }
    for (const auto& lane : owner.automation) {
        if (lane.points.empty()) continue;
        const auto& target = lane.target;
        std::string parameter;
        if (target.kind == AutomationTarget::Kind::parameter) {
            const auto found = parameter_ids_.find({key_of(target.processor), target.parameter_index});
            if (found != parameter_ids_.end()) parameter = found->second;
        } else if (const auto* channel = channel_of(target.processor)) {
            parameter = target.kind == AutomationTarget::Kind::gain  ? channel->volume
                      : target.kind == AutomationTarget::Kind::pan   ? channel->pan
                                                                     : channel->mute;
        }
        if (parameter.empty()) continue;
        auto node = points(lanes);
        node.append_child("Target").append_attribute("parameter") = parameter.c_str();
        const auto add = [&](Tick at, double value, const char* interpolation) {
            auto point = node.append_child(target.kind == AutomationTarget::Kind::mute ? "BoolPoint" : "RealPoint");
            point.append_attribute("time") = number(beats(static_cast<double>(at))).c_str();
            if (target.kind == AutomationTarget::Kind::mute) {
                point.append_attribute("value") = boolean(value >= 0.5);
                ++report.automation_points;
                return;
            }
            point.append_attribute("value") = number(value).c_str();
            point.append_attribute("interpolation") = interpolation;
            ++report.automation_points;
        };
        switch (target.kind) {
        case AutomationTarget::Kind::gain: {
            // The lane ramps in decibels; the channel's volume is linear, so
            // a ramp is written as a point every sixteenth along it.
            node.append_attribute("unit") = "linear";
            const Tick stride = std::max<Tick>(1, tpb_ / 4);
            for (std::size_t index = 0; index < lane.points.size(); ++index) {
                const auto& point = lane.points[index];
                add(point.at, db_to_linear(point.value), "linear");
                if (index + 1 >= lane.points.size()) break;
                const auto& next = lane.points[index + 1];
                if (next.at <= point.at || next.value == point.value) continue;
                for (Tick at = point.at + stride; at < next.at; at += stride) {
                    const double t = static_cast<double>(at - point.at) / static_cast<double>(next.at - point.at);
                    add(at, db_to_linear(point.value + (next.value - point.value) * t), "linear");
                }
            }
            break;
        }
        case AutomationTarget::Kind::pan:
            node.append_attribute("unit") = "normalized";
            for (const auto& point : lane.points) add(point.at, (point.value + 1.0) / 2.0, "linear");
            break;
        case AutomationTarget::Kind::mute:
            for (const auto& point : lane.points) add(point.at, point.value, "hold");
            break;
        case AutomationTarget::Kind::parameter:
            node.append_attribute("unit") = "linear";
            for (const auto& point : lane.points) add(point.at, point.value, "linear");
            break;
        }
    }
}

bool Writer::write(const std::filesystem::path& path, std::string* error) {
    std::string why;
    if (!song_.consistent(&why)) return fail(error, "the song is inconsistent: " + why);
    arranged_.reserve(song_.tracks.size());
    for (std::size_t track = 0; track < song_.tracks.size(); ++track)
        arranged_.push_back(song_.arrange(track, 0));
    collect_parameters();

    pugi::xml_document document;
    auto declaration = document.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";
    declaration.append_attribute("standalone") = "yes";
    auto project = document.append_child("Project");
    project.append_attribute("version") = "1.0";
    auto application = project.append_child("Application");
    application.append_attribute("name") = "Blokkily";
    application.append_attribute("version") = options_.application_version.c_str();
    write_transport(project);

    const auto make_ids = [this] { return ChannelIds{id(), id(), id(), id()}; };
    for (std::size_t index = 0; index < song_.tracks.size(); ++index) tracks_.push_back(make_ids());
    for (std::size_t index = 0; index < song_.returns.size(); ++index) returns_.push_back(make_ids());
    master_ = make_ids();

    auto structure = project.append_child("Structure");
    std::vector<std::string> track_ids;
    for (std::size_t index = 0; index < song_.tracks.size(); ++index) {
        const auto& source = song_.tracks[index];
        auto track = structure.append_child("Track");
        track_ids.push_back(id());
        track.append_attribute("id") = track_ids.back().c_str();
        track.append_attribute("name") = source.name.c_str();
        const bool audio = std::any_of(song_.audio_clips.begin(), song_.audio_clips.end(),
                                       [&](const AudioClip& clip) { return clip.track == index && !clip.freeze; });
        track.append_attribute("contentType") = audio ? "notes audio" : "notes";
        track.append_attribute("loaded") = "true";
        write_channel(track, tracks_[index], "regular", source.mix, source.mix.gain_db, master_.channel);
        auto channel = track.child("Channel");
        auto devices = channel.child("Devices");
        const auto where = static_cast<std::uint32_t>(index);
        if (source.source) {
            omit("Track \"" + source.name + "\": played by another track's instrument output, which DAWproject cannot route");
        } else if (!source.instrument.format.empty()) {
            write_device(devices, track_instrument(where), source.instrument, false, nullptr);
        }
        for (std::size_t slot = 0; slot < source.inserts.size(); ++slot) {
            const auto& effect = source.inserts[slot];
            write_device(devices, {BusKind::track, where, static_cast<std::int32_t>(slot)}, effect.plugin,
                         effect.bypass, &effect.parameters);
            if (effect.sidechain)
                omit(bus_label(song_, {BusKind::track, where, 0}) + ": the sidechain key of insert " +
                     std::to_string(slot + 1));
        }
        auto sends = channel.child("Sends");
        for (const auto& send : source.sends) {
            if (send.bus >= returns_.size()) continue;
            auto element = sends.append_child("Send");
            element.append_attribute("id") = id().c_str();
            element.append_attribute("destination") = returns_[send.bus].channel.c_str();
            element.append_attribute("type") = send.pre_fader ? "pre" : "post";
            auto volume = element.append_child("Volume");
            const double linear = db_to_linear(send.level_db);
            volume.append_attribute("id") = id().c_str();
            volume.append_attribute("name") = "Send";
            volume.append_attribute("unit") = "linear";
            volume.append_attribute("min") = "0";
            volume.append_attribute("max") = number(std::max(2.0, linear)).c_str();
            volume.append_attribute("value") = number(linear).c_str();
        }
        if (source.arp.enabled)
            omit("Track \"" + source.name + "\": its arpeggiator (the notes are written as played into it)");
        ++report.tracks;
    }
    for (std::size_t index = 0; index < song_.returns.size(); ++index) {
        const auto& source = song_.returns[index];
        auto track = structure.append_child("Track");
        track.append_attribute("id") = id().c_str();
        track.append_attribute("name") = source.name.c_str();
        track.append_attribute("contentType") = "audio";
        track.append_attribute("loaded") = "true";
        write_channel(track, returns_[index], "effect", source.mix, source.mix.gain_db, master_.channel);
        auto devices = track.child("Channel").child("Devices");
        for (std::size_t slot = 0; slot < source.inserts.size(); ++slot)
            write_device(devices, {BusKind::ret, static_cast<std::uint32_t>(index), static_cast<std::int32_t>(slot)},
                         source.inserts[slot].plugin, source.inserts[slot].bypass, &source.inserts[slot].parameters);
    }
    {
        auto track = structure.append_child("Track");
        track.append_attribute("id") = id().c_str();
        track.append_attribute("name") = "Master";
        track.append_attribute("contentType") = "audio notes";
        track.append_attribute("loaded") = "true";
        write_channel(track, master_, "master", MixerStrip{}, song_.master_gain_db, "");
        auto devices = track.child("Channel").child("Devices");
        for (std::size_t slot = 0; slot < song_.master_inserts.size(); ++slot)
            write_device(devices, {BusKind::master, 0, static_cast<std::int32_t>(slot)},
                         song_.master_inserts[slot].plugin, song_.master_inserts[slot].bypass,
                         &song_.master_inserts[slot].parameters);
    }
    // Elements the schema allows to be empty are still better left out.
    for (auto track : structure.children("Track")) {
        auto channel = track.child("Channel");
        if (!channel.child("Devices").first_child()) channel.remove_child("Devices");
        if (!channel.child("Sends").first_child()) channel.remove_child("Sends");
    }

    auto arrangement = project.append_child("Arrangement");
    arrangement.append_attribute("id") = id().c_str();
    auto lanes = arrangement.append_child("Lanes");
    lanes.append_attribute("id") = id().c_str();
    lanes.append_attribute("timeUnit") = "beats";
    for (std::size_t index = 0; index < song_.tracks.size(); ++index) {
        auto lane = lanes.append_child("Lanes");
        lane.append_attribute("id") = id().c_str();
        lane.append_attribute("track") = track_ids[index].c_str();
        auto clips = lane.append_child("Clips");
        clips.append_attribute("id") = id().c_str();
        write_clips(clips, index);
        write_audio_clips(clips, index);
        write_automation(lane, index);
    }
    // The tempo map: a ramp is a linear segment to the next point.
    auto tempo = arrangement.append_child("TempoAutomation");
    tempo.append_attribute("id") = id().c_str();
    tempo.append_attribute("unit") = "bpm";
    tempo.append_child("Target").append_attribute("parameter") = tempo_id_.c_str();
    for (const auto& point : song_.tempo.points) {
        auto element = tempo.append_child("RealPoint");
        element.append_attribute("time") = number(beats(static_cast<double>(point.at))).c_str();
        element.append_attribute("value") = number(point.bpm).c_str();
        element.append_attribute("interpolation") = point.ramp ? "linear" : "hold";
        ++report.automation_points;
    }
    auto meters = arrangement.append_child("TimeSignatureAutomation");
    meters.append_attribute("id") = id().c_str();
    meters.append_child("Target").append_attribute("parameter") = meter_id_.c_str();
    for (const auto& change : song_.meter.changes) {
        auto element = meters.append_child("TimeSignaturePoint");
        // Bars are counted at 480 ticks a quarter note whatever the song's
        // resolution (timebase.hpp).
        element.append_attribute("time") = number(static_cast<double>(song_.meter.bar_start(change.bar)) / 480.0).c_str();
        element.append_attribute("numerator") = change.numerator;
        element.append_attribute("denominator") = change.denominator;
    }
    project.append_child("Scenes");

    if (!song_.launcher.empty()) omit("The scene launcher's scenes (only the arrangement is exported)");
    if (!song_.modulators.empty())
        omit(std::to_string(song_.modulators.size()) + " LFOs, macros and envelope followers");

    pugi::xml_document metadata;
    auto meta_declaration = metadata.append_child(pugi::node_declaration);
    meta_declaration.append_attribute("version") = "1.0";
    meta_declaration.append_attribute("encoding") = "UTF-8";
    meta_declaration.append_attribute("standalone") = "yes";
    auto meta = metadata.append_child("MetaData");
    if (!options_.title.empty()) meta.append_child("Title").text() = options_.title.c_str();
    if (!song_.notes.empty()) meta.append_child("Comment").text() = song_.notes.c_str();

    const auto text_of = [](const pugi::xml_document& doc) {
        std::ostringstream out;
        doc.save(out, "  ", pugi::format_default, pugi::encoding_utf8);
        return out.str();
    };
    const auto project_text = text_of(document);
    const auto metadata_text = text_of(metadata);

    juce::ZipFile::Builder builder;
    const auto add_bytes = [&](const std::string& name, const void* data, std::size_t size, int level) {
        builder.addEntry(std::make_unique<juce::MemoryInputStream>(data, size, true), level,
                         juce::String::fromUTF8(name.c_str()), archive_time());
    };
    add_bytes("project.xml", project_text.data(), project_text.size(), 9);
    add_bytes("metadata.xml", metadata_text.data(), metadata_text.size(), 9);
    for (const auto& [name, bytes] : blobs_) add_bytes(name, bytes.data(), bytes.size(), 9);
    for (const auto& [name, source] : files_) {
        auto stream = std::make_unique<juce::FileInputStream>(juce::File(juce::String::fromUTF8(source.string().c_str())));
        if (!stream->openedOk()) return fail(error, "cannot read " + source.string());
        // Audio barely compresses; storing it keeps a long song's export quick.
        builder.addEntry(std::move(stream), 0, juce::String::fromUTF8(name.c_str()), archive_time());
    }

    // Written beside the destination and moved over it, so a failed export
    // never leaves half a file where a good one was.
    auto partial = path;
    partial += ".part";
    {
        const juce::File target(juce::String::fromUTF8(partial.string().c_str()));
        (void)target.deleteFile();
        juce::FileOutputStream out(target);
        if (!out.openedOk()) return fail(error, "cannot open " + partial.string() + " for writing");
        if (!builder.writeToStream(out, nullptr)) return fail(error, "cannot write the archive");
        out.flush();
        if (out.getStatus().failed()) return fail(error, out.getStatus().getErrorMessage().toStdString());
    }
    std::error_code moved;
    std::filesystem::rename(partial, path, moved);
    if (moved) {
        std::filesystem::remove(partial, moved);
        return fail(error, "cannot replace " + path.string());
    }
    return true;
}

// --- Reading ----------------------------------------------------------------

double attribute_number(pugi::xml_node node, const char* name, double fallback = 0.0) {
    const auto text = std::string_view(node.attribute(name).value());
    double value = fallback;
    if (text.empty() || !read_number_text(text, value)) return fallback;
    return value;
}

DawProjectPoints read_points(pugi::xml_node node) {
    DawProjectPoints result;
    auto target = node.child("Target");
    result.target_parameter = target.attribute("parameter").value();
    result.expression = target.attribute("expression").value();
    result.controller = target.attribute("controller").as_int(-1);
    result.key = target.attribute("key").as_int(-1);
    result.unit = node.attribute("unit").value();
    for (auto point : node.children()) {
        const std::string_view name = point.name();
        if (name == "Target") continue;
        DawProjectPoint read;
        read.time = attribute_number(point, "time");
        if (name == "BoolPoint")
            read.value = point.attribute("value").as_bool() ? 1.0 : 0.0;
        else
            read.value = attribute_number(point, "value");
        read.interpolation = point.attribute("interpolation").value();
        read.numerator = point.attribute("numerator").as_int(0);
        read.denominator = point.attribute("denominator").as_int(0);
        result.points.push_back(read);
    }
    return result;
}

DawProjectNote read_note(pugi::xml_node node) {
    DawProjectNote note;
    note.time = attribute_number(node, "time");
    note.duration = attribute_number(node, "duration");
    note.channel = node.attribute("channel").as_int();
    note.key = node.attribute("key").as_int();
    note.velocity = attribute_number(node, "vel");
    for (auto points : node.children("Points")) note.expressions.push_back(read_points(points));
    for (auto points : node.child("Lanes").children("Points")) note.expressions.push_back(read_points(points));
    return note;
}

void read_clip_content(pugi::xml_node node, DawProjectClip& clip) {
    for (auto child : node.children()) {
        const std::string_view name = child.name();
        if (name == "Notes") {
            for (auto note : child.children("Note")) clip.notes.push_back(read_note(note));
        } else if (name == "Points") {
            clip.points.push_back(read_points(child));
        } else if (name == "Lanes") {
            read_clip_content(child, clip);
        } else if (name == "Warps") {
            auto audio = child.child("Audio");
            clip.audio_path = audio.child("File").attribute("path").value();
            clip.audio_seconds = attribute_number(audio, "duration");
            clip.audio_channels = audio.attribute("channels").as_int();
            clip.audio_sample_rate = audio.attribute("sampleRate").as_int();
            for (auto warp : child.children("Warp"))
                clip.warps.push_back({attribute_number(warp, "time"), attribute_number(warp, "contentTime")});
        }
    }
}

DawProjectChannel read_channel(pugi::xml_node node) {
    DawProjectChannel channel;
    channel.id = node.attribute("id").value();
    channel.role = node.attribute("role").value();
    channel.destination = node.attribute("destination").value();
    channel.solo = node.attribute("solo").as_bool();
    channel.volume = attribute_number(node.child("Volume"), "value", 1.0);
    channel.volume_id = node.child("Volume").attribute("id").value();
    channel.pan = attribute_number(node.child("Pan"), "value", 0.5);
    channel.pan_id = node.child("Pan").attribute("id").value();
    channel.mute = node.child("Mute").attribute("value").as_bool();
    channel.mute_id = node.child("Mute").attribute("id").value();
    for (auto device : node.child("Devices").children()) {
        DawProjectDevice read;
        read.element = device.name();
        read.id = device.attribute("id").value();
        read.device_id = device.attribute("deviceID").value();
        read.device_name = device.attribute("deviceName").value();
        read.device_role = device.attribute("deviceRole").value();
        read.state_path = device.child("State").attribute("path").value();
        for (auto parameter : device.child("Parameters").children()) {
            DawProjectParameter value;
            value.id = parameter.attribute("id").value();
            value.name = parameter.attribute("name").value();
            value.parameter_id = parameter.attribute("parameterID").as_llong(-1);
            value.value = attribute_number(parameter, "value");
            value.min = attribute_number(parameter, "min", 0.0);
            value.max = attribute_number(parameter, "max", 1.0);
            read.parameters.push_back(value);
        }
        channel.devices.push_back(std::move(read));
    }
    for (auto send : node.child("Sends").children("Send"))
        channel.sends.push_back({send.attribute("destination").value(),
                                 attribute_number(send.child("Volume"), "value"),
                                 send.attribute("type").value()});
    return channel;
}

std::optional<std::vector<std::byte>> entry_bytes(juce::ZipFile& zip, const std::string& name) {
    const auto index = zip.getIndexOfFileName(juce::String::fromUTF8(name.c_str()));
    if (index < 0) return std::nullopt;
    std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(index));
    if (stream == nullptr) return std::nullopt;
    juce::MemoryBlock block;
    stream->readIntoMemoryBlock(block);
    std::vector<std::byte> bytes(block.getSize());
    if (!bytes.empty()) std::memcpy(bytes.data(), block.getData(), block.getSize());
    return bytes;
}

} // namespace

std::optional<DawProjectReport> write_dawproject(const std::filesystem::path& path, const Song& song,
                                                 const DawProjectOptions& options, std::string* error) {
    Writer writer(song, options);
    if (!writer.write(path, error)) return std::nullopt;
    return std::move(writer.report);
}

const DawProjectTrack* DawProjectFile::track(const std::string& name) const {
    for (const auto& candidate : tracks)
        if (candidate.name == name) return &candidate;
    return nullptr;
}

const DawProjectTrack* DawProjectFile::track_of_channel(const std::string& channel_id) const {
    for (const auto& candidate : tracks)
        if (candidate.channel.id == channel_id) return &candidate;
    return nullptr;
}

std::optional<DawProjectFile> read_dawproject(const std::filesystem::path& path, std::string* error) {
    const juce::File source(juce::String::fromUTF8(path.string().c_str()));
    if (!source.existsAsFile()) return nothing(error, "no file at " + path.string());
    juce::ZipFile zip(source);
    DawProjectFile file;
    for (int index = 0; index < zip.getNumEntries(); ++index)
        file.entries.push_back(zip.getEntry(index)->filename.toStdString());
    const auto project_bytes = entry_bytes(zip, "project.xml");
    if (!project_bytes) return nothing(error, "the archive holds no project.xml");
    file.project_xml.assign(reinterpret_cast<const char*>(project_bytes->data()), project_bytes->size());
    pugi::xml_document document;
    if (!document.load_string(file.project_xml.c_str()))
        return nothing(error, "project.xml is not well-formed XML");
    auto project = document.child("Project");
    if (!project || std::string_view(project.attribute("version").value()) != "1.0")
        return nothing(error, "project.xml is not a DAWproject 1.0 project");
    file.application = project.child("Application").attribute("name").value();
    auto transport = project.child("Transport");
    file.tempo_id = transport.child("Tempo").attribute("id").value();
    file.tempo = attribute_number(transport.child("Tempo"), "value", 120.0);
    file.numerator = transport.child("TimeSignature").attribute("numerator").as_int(4);
    file.denominator = transport.child("TimeSignature").attribute("denominator").as_int(4);
    for (auto track : project.child("Structure").children("Track")) {
        DawProjectTrack read;
        read.id = track.attribute("id").value();
        read.name = track.attribute("name").value();
        read.content_type = track.attribute("contentType").value();
        read.channel = read_channel(track.child("Channel"));
        for (auto& device : read.channel.devices)
            if (!device.state_path.empty())
                if (auto bytes = entry_bytes(zip, device.state_path)) device.state = std::move(*bytes);
        file.tracks.push_back(std::move(read));
    }
    auto arrangement = project.child("Arrangement");
    for (auto lane : arrangement.child("Lanes").children("Lanes")) {
        const std::string owner = lane.attribute("track").value();
        auto found = std::find_if(file.tracks.begin(), file.tracks.end(),
                                  [&](const DawProjectTrack& track) { return track.id == owner; });
        if (found == file.tracks.end()) continue;
        for (auto clip : lane.child("Clips").children("Clip")) {
            DawProjectClip read;
            read.time = attribute_number(clip, "time");
            read.duration = attribute_number(clip, "duration");
            read.name = clip.attribute("name").value();
            read.enabled = clip.attribute("enable").as_bool(true);
            read.fade_in = attribute_number(clip, "fadeInTime");
            read.fade_out = attribute_number(clip, "fadeOutTime");
            read_clip_content(clip, read);
            found->clips.push_back(std::move(read));
        }
        for (auto points : lane.children("Points")) found->automation.push_back(read_points(points));
    }
    file.tempo_automation = read_points(arrangement.child("TempoAutomation")).points;
    file.meter_automation = read_points(arrangement.child("TimeSignatureAutomation")).points;
    if (const auto metadata_bytes = entry_bytes(zip, "metadata.xml")) {
        pugi::xml_document metadata;
        if (metadata.load_buffer(metadata_bytes->data(), metadata_bytes->size())) {
            file.title = metadata.child("MetaData").child("Title").text().get();
            file.comment = metadata.child("MetaData").child("Comment").text().get();
        }
    }
    return file;
}

} // namespace blokkily
