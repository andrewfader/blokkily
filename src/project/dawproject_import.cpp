// Opening a .dawproject as a song (dawproject.hpp, features/dawproject.feature):
// the file read_dawproject() reads, turned into what Blokkily plays.

#include "blokkily/project/dawproject.hpp"

#include "blokkily/audio/audio_file.hpp"
#include "blokkily/sequencer/take.hpp"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>

namespace blokkily {
namespace {

constexpr Tick ticks_per_beat = 480;

Tick ticks(double beats) { return static_cast<Tick>(std::llround(beats * ticks_per_beat)); }

double linear_to_db(double linear) {
    return linear > 1e-6 ? 20.0 * std::log10(linear) : -96.0;
}

// A device parameter's own index: its parameterID when the file gave one (a
// CLAP parameter's id is the plugin's own), else the number Blokkily writes
// in a parameter's name ("Parameter 3").
std::optional<std::int32_t> parameter_index(const DawProjectParameter& parameter) {
    if (parameter.parameter_id >= 0 && parameter.parameter_id <= INT32_MAX)
        return static_cast<std::int32_t>(parameter.parameter_id);
    constexpr std::string_view prefix = "Parameter ";
    if (parameter.name.rfind(prefix, 0) == 0) {
        try {
            return static_cast<std::int32_t>(std::stol(parameter.name.substr(prefix.size())));
        } catch (...) {
        }
    }
    return std::nullopt;
}

// What a clip's notes and controllers are, read relative to its start: the
// part a section holds for one track.
struct Content {
    std::vector<PlayedNote> notes;
    std::vector<ContinuousEvent> controls;

    bool operator==(const Content& other) const {
        if (notes.size() != other.notes.size() || controls != other.controls) return false;
        for (std::size_t index = 0; index < notes.size(); ++index) {
            const auto& a = notes[index];
            const auto& b = other.notes[index];
            if (a.start != b.start || a.duration != b.duration || a.key != b.key ||
                a.velocity != b.velocity || a.cents != b.cents || a.expression != b.expression)
                return false;
        }
        return true;
    }
};

NoteExpression::Kind expression_kind(const std::string& expression) {
    if (expression == "timbre") return NoteExpression::Kind::timbre;
    if (expression == "pressure") return NoteExpression::Kind::pressure;
    return NoteExpression::Kind::pitch;
}

Content content_of(const DawProjectClip& clip, std::vector<std::string>& omitted, const std::string& where) {
    Content content;
    std::size_t skipped_expression = 0;
    for (const auto& read : clip.notes) {
        if (read.key < 0 || read.key > 127 || read.duration <= 0.0) continue;
        PlayedNote note;
        note.start = ticks(read.time);
        note.duration = std::max<Tick>(1, ticks(read.duration));
        note.key = static_cast<std::int16_t>(read.key);
        note.velocity = static_cast<float>(std::clamp(read.velocity, 0.0, 1.0));
        for (const auto& lane : read.expressions) {
            if (lane.expression == "transpose") {
                // A retune at the note's start is its tuning; what follows
                // moves its pitch from there.
                double retune = 0.0;
                for (const auto& point : lane.points) {
                    if (point.time <= 0.0 && lane.points.front().time == point.time) {
                        retune = point.value;
                        note.cents = retune * 100.0;
                        continue;
                    }
                    note.expression.push_back({0, ticks(point.time), NoteExpression::Kind::pitch,
                                               static_cast<float>(point.value - retune)});
                }
            } else if (lane.expression == "timbre" || lane.expression == "pressure") {
                for (const auto& point : lane.points)
                    note.expression.push_back({0, ticks(point.time), expression_kind(lane.expression),
                                               static_cast<float>(std::clamp(point.value, 0.0, 1.0))});
            } else {
                ++skipped_expression;
            }
        }
        content.notes.push_back(std::move(note));
    }
    for (const auto& lane : clip.points) {
        ContinuousEvent::Kind kind;
        std::uint8_t controller = 0;
        double full = 127.0;
        if (lane.expression == "pitchBend") {
            kind = ContinuousEvent::Kind::pitch_bend;
            full = 16383.0;
        } else if (lane.expression == "channelController" && lane.controller >= 0 && lane.controller < 128) {
            kind = ContinuousEvent::Kind::control_change;
            controller = static_cast<std::uint8_t>(lane.controller);
        } else if (lane.expression == "channelPressure") {
            kind = ContinuousEvent::Kind::channel_pressure;
        } else if (lane.expression == "polyPressure" && lane.key >= 0 && lane.key < 128) {
            kind = ContinuousEvent::Kind::poly_pressure;
            controller = static_cast<std::uint8_t>(lane.key);
        } else {
            continue; // parameter lanes are read as automation
        }
        for (const auto& point : lane.points)
            content.controls.push_back({ticks(point.time), kind, controller,
                                        static_cast<std::uint16_t>(std::lround(std::clamp(point.value, 0.0, 1.0) * full))});
    }
    if (skipped_expression > 0)
        omitted.push_back(where + ": " + std::to_string(skipped_expression) +
                          " note expression lanes of a kind Blokkily has no place for");
    return content;
}

class Importer {
public:
    Importer(const DawProjectFile& file, const std::filesystem::path& source,
             const std::filesystem::path& audio_dir, const DawProjectLocator& locate)
        : file_(file), source_(source), audio_dir_(audio_dir), locate_(locate) {}

    bool run(std::string* error) {
        auto& song = result.song;
        song.patterns.clear();
        song.tracks.clear();
        song.clips.clear();
        song.sections.clear();
        read_time();
        // Returns first, so a send can name its return by channel.
        for (const auto& track : file_.tracks)
            if (track.channel.role == "effect") add_return(track);
        for (const auto& track : file_.tracks) {
            if (track.channel.role == "effect" || track.channel.role == "master") continue;
            add_track(track);
        }
        for (const auto& track : file_.tracks)
            if (track.channel.role == "master") add_master(track);
        if (song.tracks.empty()) {
            if (error != nullptr) *error = "the file has no tracks Blokkily can play";
            return false;
        }
        build_sections();
        read_automation();
        // A song with nothing placed still needs a section to edit.
        if (song.sections.empty()) {
            const auto first = song.add_section("PATTERN", 1920, ticks_per_beat);
            (void)first;
        }
        std::string why;
        if (!song.consistent(&why)) {
            if (error != nullptr) *error = "the file did not make a playable song: " + why;
            return false;
        }
        return true;
    }

    DawProjectImport result;

private:
    void omit(std::string line) { result.omitted.push_back(std::move(line)); }

    void read_time() {
        auto& song = result.song;
        song.tempo.points = {{0, std::clamp(file_.tempo, minimum_bpm, maximum_bpm), false}};
        for (const auto& point : file_.tempo_automation)
            song.tempo.set({std::max<Tick>(0, ticks(point.time)), std::clamp(point.value, minimum_bpm, maximum_bpm),
                            point.interpolation == "linear"});
        song.meter.changes = {{0, static_cast<std::int16_t>(std::clamp(file_.numerator, 1, 64)),
                               static_cast<std::int16_t>(std::clamp(file_.denominator, 1, 32))}};
        // A time signature point is placed in beats; the song keeps it by bar.
        double at_beat = 0.0;
        std::int32_t bar = 0;
        auto meter = song.meter.changes.front();
        for (const auto& point : file_.meter_automation) {
            if (point.numerator <= 0 || point.denominator <= 0) continue;
            const double bar_beats = 4.0 * meter.numerator / meter.denominator;
            bar += static_cast<std::int32_t>(std::lround((point.time - at_beat) / bar_beats));
            at_beat = point.time;
            meter = {bar, static_cast<std::int16_t>(std::clamp(point.numerator, 1, 64)),
                     static_cast<std::int16_t>(std::clamp(point.denominator, 1, 32))};
            song.meter.set(meter);
        }
    }

    MixerStrip strip_of(const DawProjectChannel& channel) {
        MixerStrip strip;
        strip.gain_db = std::clamp(linear_to_db(channel.volume), -96.0, 12.0);
        strip.pan = std::clamp(channel.pan * 2.0 - 1.0, -1.0, 1.0);
        strip.mute = channel.mute;
        strip.solo = channel.solo;
        return strip;
    }

    // The slot a device names: a plugin installed here, a built-in effect, or
    // what a Blokkily placeholder stands for.
    std::optional<PluginSlot> slot_of(const DawProjectDevice& device, ProcessorAddress where,
                                      const std::string& owner) {
        const std::string label = owner + ": " + (device.device_name.empty() ? device.element : device.device_name);
        if (device.element == "ClapPlugin" || device.element == "Vst3Plugin") {
            auto slot = locate_ ? locate_(device) : std::nullopt;
            if (!slot) {
                omit(label + ": not installed here");
                return std::nullopt;
            }
            if (device.element == "ClapPlugin") slot->state = device.state;
            else if (!device.state.empty()) result.presets.push_back({where, device.state});
            ++result.devices;
            return slot;
        }
        if (device.element == "BuiltinDevice") {
            PluginSlot slot{"Built-in", "", device.device_name, {}};
            ++result.devices;
            return slot;
        }
        if (!device.state.empty())
            if (auto slot = dawproject_slot(device.state)) {
                ++result.devices;
                return slot;
            }
        omit(label + ": a device Blokkily cannot load");
        return std::nullopt;
    }

    std::vector<EffectSlot> effects_of(const DawProjectChannel& channel, BusKind kind, std::uint32_t bus,
                                       const std::string& owner, bool skip_instrument) {
        std::vector<EffectSlot> inserts;
        bool instrument_seen = false;
        for (const auto& device : channel.devices) {
            if (skip_instrument && device.device_role == "instrument" && !instrument_seen) {
                instrument_seen = true;
                continue;
            }
            const ProcessorAddress where{kind, bus, static_cast<std::int32_t>(inserts.size())};
            auto slot = slot_of(device, where, owner);
            if (!slot) continue;
            EffectSlot effect;
            effect.plugin = *slot;
            if (device.element == "BuiltinDevice")
                for (const auto& parameter : device.parameters)
                    if (const auto index = parameter_index(parameter))
                        effect.parameters.push_back({*index, parameter.value});
            record_parameters(device, where);
            inserts.push_back(std::move(effect));
        }
        return inserts;
    }

    // A device's parameters by their element id, for the automation lanes.
    void record_parameters(const DawProjectDevice& device, ProcessorAddress where) {
        for (const auto& parameter : device.parameters)
            if (const auto index = parameter_index(parameter))
                parameters_[parameter.id] = {where, *index};
    }

    void add_return(const DawProjectTrack& track) {
        auto& song = result.song;
        ReturnBus bus;
        bus.name = track.name.empty() ? "Return" : track.name;
        bus.mix = strip_of(track.channel);
        const auto index = static_cast<std::uint32_t>(song.returns.size());
        bus.inserts = effects_of(track.channel, BusKind::ret, index, "Return \"" + bus.name + "\"", false);
        returns_[track.channel.id] = index;
        channels_[track.channel.id] = {BusKind::ret, index, -1};
        song.returns.push_back(std::move(bus));
    }

    void add_master(const DawProjectTrack& track) {
        auto& song = result.song;
        song.master_gain_db = std::clamp(linear_to_db(track.channel.volume), -96.0, 12.0);
        song.master_inserts = effects_of(track.channel, BusKind::master, 0, "The master", false);
        channels_[track.channel.id] = {BusKind::master, 0, -1};
    }

    void add_track(const DawProjectTrack& source) {
        auto& song = result.song;
        Track track;
        track.name = source.name.empty() ? "Track " + std::to_string(song.tracks.size() + 1) : source.name;
        track.mix = strip_of(source.channel);
        const auto index = static_cast<std::uint32_t>(song.tracks.size());
        const std::string owner = "Track \"" + track.name + "\"";
        for (const auto& device : source.channel.devices)
            if (device.device_role == "instrument") {
                if (auto slot = slot_of(device, track_instrument(index), owner)) track.instrument = *slot;
                record_parameters(device, track_instrument(index));
                break;
            }
        track.inserts = effects_of(source.channel, BusKind::track, index, owner, true);
        for (const auto& send : source.channel.sends) {
            const auto found = returns_.find(send.destination);
            if (found == returns_.end()) continue;
            track.sends.push_back({found->second, std::clamp(linear_to_db(send.volume), -96.0, 12.0),
                                   send.type == "pre"});
        }
        channels_[source.channel.id] = {BusKind::track, index, -1};
        song.tracks.push_back(std::move(track));
        sources_.push_back(&source);
        ++result.tracks;
    }

    // Every clip of every track: notes as sections and placements, audio as
    // audio clips. Clips named and as long as one already read share its
    // section; a track's part there is the first content it placed, and a
    // placement that differs gets a section of its own.
    void build_sections() {
        auto& song = result.song;
        struct Group {
            std::string name;
            Tick length;
            std::size_t section;
            std::map<std::size_t, Content> parts;
        };
        std::vector<Group> groups;
        std::map<std::string, int> used_names;
        for (std::size_t track = 0; track < sources_.size(); ++track) {
            const auto& source = *sources_[track];
            const std::string owner = "Track \"" + song.tracks[track].name + "\"";
            for (const auto& clip : source.clips) {
                if (!clip.audio_path.empty()) {
                    add_audio_clip(track, clip, owner);
                    continue;
                }
                const Tick length = std::max<Tick>(1, ticks(clip.duration));
                auto content = content_of(clip, result.omitted, owner);
                Group* group = nullptr;
                for (auto& candidate : groups) {
                    if (candidate.name != clip.name || candidate.length != length) continue;
                    const auto part = candidate.parts.find(track);
                    if (part == candidate.parts.end() || part->second == content) {
                        group = &candidate;
                        break;
                    }
                }
                if (group == nullptr) {
                    std::string name = clip.name.empty() ? "CLIP" : clip.name;
                    const int seen = used_names[name]++;
                    if (seen > 0) name += " " + std::to_string(seen + 1);
                    groups.push_back({clip.name, length, song.add_section(name, length, ticks_per_beat), {}});
                    group = &groups.back();
                }
                const auto part = *song.part(group->section, track);
                if (!group->parts.contains(track)) {
                    auto& pattern = song.patterns[part].pattern;
                    for (const auto& note : content.notes) {
                        (void)write_played(pattern, note, ticks_per_step(), true);
                        ++result.notes;
                    }
                    for (const auto& control : content.controls) write_continuous(pattern, control);
                    group->parts[track] = std::move(content);
                }
                Clip placed;
                placed.track = track;
                placed.pattern = part;
                placed.start = std::max<Tick>(0, ticks(clip.time));
                placed.comped_out = !clip.enabled;
                song.clips.push_back(placed);
                ++result.clips;
                // Parameter lanes inside a clip are automation, from where it is.
                for (const auto& lane : clip.points)
                    if (!lane.target_parameter.empty())
                        add_automation_points(lane, clip.time, owner);
            }
        }
    }

    static Tick ticks_per_step() { return ticks_per_beat / 4; }

    void add_audio_clip(std::size_t track, const DawProjectClip& clip, const std::string& owner) {
        auto& song = result.song;
        const auto file = extract(clip.audio_path);
        if (!file) {
            omit(owner + ": the audio file " + clip.audio_path + " could not be read");
            return;
        }
        AudioClip placed;
        placed.id = song.next_audio_clip_id();
        placed.track = track;
        placed.file = *file;
        placed.start = std::max<Tick>(0, ticks(clip.time));
        const auto& ref = song.audio_files[*file];
        const double rate = static_cast<double>(ref.sample_rate);
        const double from = clip.warps.empty() ? 0.0 : clip.warps.front().content_time;
        const double to = clip.warps.empty() ? clip.audio_seconds : clip.warps.back().content_time;
        placed.offset_frames = static_cast<std::uint64_t>(std::llround(std::max(0.0, from) * rate));
        const auto end = std::min<std::uint64_t>(ref.frames, static_cast<std::uint64_t>(std::llround(to * rate)));
        placed.length_frames = end > placed.offset_frames ? end - placed.offset_frames : 0;
        placed.fade_in_frames = static_cast<std::uint64_t>(std::llround(clip.fade_in * rate));
        placed.fade_out_frames = static_cast<std::uint64_t>(std::llround(clip.fade_out * rate));
        placed.comped_out = !clip.enabled;
        if (placed.length_frames == 0) return;
        // Played at its own speed: a warp that stretched it is not kept.
        if (clip.warps.size() >= 2) {
            const double beats_long = clip.warps.back().time - clip.warps.front().time;
            const double seconds_long = song.tempo.seconds_at_tick(static_cast<double>(placed.start) + beats_long * ticks_per_beat, ticks_per_beat) -
                                        song.tempo.seconds_at_tick(static_cast<double>(placed.start), ticks_per_beat);
            if (std::abs(seconds_long - (to - from)) > 0.01 * std::max(0.01, to - from))
                omit(owner + ": an audio clip's warp (it plays at its own speed)");
        }
        song.audio_clips.push_back(placed);
        ++result.audio_clips;
    }

    // An audio file of the archive, written into the audio folder once.
    std::optional<std::size_t> extract(const std::string& entry) {
        const auto known = extracted_.find(entry);
        if (known != extracted_.end()) return known->second;
        const juce::File source(juce::String::fromUTF8(source_.string().c_str()));
        juce::ZipFile zip(source);
        const auto index = zip.getIndexOfFileName(juce::String::fromUTF8(entry.c_str()));
        if (index < 0) return std::nullopt;
        std::error_code ignored;
        std::filesystem::create_directories(audio_dir_, ignored);
        auto target = audio_dir_ / std::filesystem::path(entry).filename();
        for (int copy = 2; std::filesystem::exists(target); ++copy)
            target = audio_dir_ / (std::filesystem::path(entry).stem().string() + "-" + std::to_string(copy) +
                                   std::filesystem::path(entry).extension().string());
        std::unique_ptr<juce::InputStream> stream(zip.createStreamForEntry(index));
        if (stream == nullptr) return std::nullopt;
        juce::File written(juce::String::fromUTF8(target.string().c_str()));
        {
            juce::FileOutputStream out(written);
            if (!out.openedOk()) return std::nullopt;
            out.writeFromInputStream(*stream, -1);
        }
        const auto info = probe_audio_file(target);
        if (!info) return std::nullopt;
        auto& song = result.song;
        song.audio_files.push_back({target, info->frames, info->sample_rate, info->channels});
        extracted_[entry] = song.audio_files.size() - 1;
        return song.audio_files.size() - 1;
    }

    // The tracks' automation lanes: the strip's volume, pan and mute, and the
    // parameters of the devices read.
    void read_automation() {
        for (std::size_t track = 0; track < sources_.size(); ++track)
            for (const auto& lane : sources_[track]->automation)
                add_automation_points(lane, 0.0, "Track \"" + result.song.tracks[track].name + "\"");
        for (const auto& source : file_.tracks)
            if (source.channel.role != "regular" && source.channel.role != "")
                for (const auto& lane : source.automation)
                    add_automation_points(lane, 0.0, source.name);
    }

    void add_automation_points(const DawProjectPoints& lane, double offset_beats, const std::string& owner) {
        auto& song = result.song;
        AutomationTarget target;
        std::optional<std::size_t> holder;
        const auto channel = [&](const std::string& id) -> const DawProjectChannel* {
            for (const auto& track : file_.tracks)
                if (track.channel.volume_id == id || track.channel.pan_id == id || track.channel.mute_id == id)
                    return &track.channel;
            return nullptr;
        };
        if (const auto* strip = channel(lane.target_parameter)) {
            const auto found = channels_.find(strip->id);
            if (found == channels_.end()) return;
            target.processor = found->second;
            target.kind = lane.target_parameter == strip->volume_id ? AutomationTarget::Kind::gain
                        : lane.target_parameter == strip->pan_id    ? AutomationTarget::Kind::pan
                                                                    : AutomationTarget::Kind::mute;
        } else if (const auto found = parameters_.find(lane.target_parameter); found != parameters_.end()) {
            target.kind = AutomationTarget::Kind::parameter;
            target.processor = found->second.first;
            target.parameter_index = found->second.second;
        } else {
            if (!lane.target_parameter.empty())
                omit(owner + ": automation of a parameter Blokkily did not load");
            return;
        }
        // A lane lives on a track: its own strip's or device's, or for a
        // return's or the master's, the first track.
        holder = target.processor.kind == BusKind::track ? target.processor.bus : 0;
        if (*holder >= song.tracks.size()) return;
        auto& lanes = song.tracks[*holder].automation;
        auto lane_of = std::find_if(lanes.begin(), lanes.end(),
                                    [&](const AutomationLane& existing) { return existing.target == target; });
        if (lane_of == lanes.end()) {
            lanes.push_back({target, {}});
            lane_of = lanes.end() - 1;
        }
        auto& points = lane_of->points;
        for (const auto& point : lane.points) {
            double value = point.value;
            if (target.kind == AutomationTarget::Kind::gain) value = std::clamp(linear_to_db(value), -96.0, 12.0);
            else if (target.kind == AutomationTarget::Kind::pan) value = std::clamp(value * 2.0 - 1.0, -1.0, 1.0);
            const Tick at = std::max<Tick>(0, ticks(offset_beats + point.time));
            // A song's lane ramps between its points: a value held until the
            // next point arrives there as a jump - the old value, then the new.
            const bool held = point.interpolation == "hold" || target.kind == AutomationTarget::Kind::mute;
            if (held && !points.empty() && points.back().at < at && points.back().value != value)
                points.push_back({at, points.back().value});
            // No tick holds more than two points.
            const auto same = std::count_if(points.begin(), points.end(), [at](const AutomationPoint& p) { return p.at == at; });
            if (same >= 2) points.erase(std::find_if(points.begin(), points.end(),
                                                     [at](const AutomationPoint& p) { return p.at == at; }) + 1);
            points.push_back({at, value});
            ++result.automation_points;
        }
        std::stable_sort(points.begin(), points.end(),
                         [](const AutomationPoint& a, const AutomationPoint& b) { return a.at < b.at; });
    }

    const DawProjectFile& file_;
    std::filesystem::path source_;
    std::filesystem::path audio_dir_;
    const DawProjectLocator& locate_;
    std::vector<const DawProjectTrack*> sources_;
    std::map<std::string, std::uint32_t> returns_;
    std::map<std::string, ProcessorAddress> channels_;
    std::map<std::string, std::pair<ProcessorAddress, std::int32_t>> parameters_;
    std::map<std::string, std::size_t> extracted_;
};

} // namespace

std::optional<DawProjectImport> import_dawproject(const std::filesystem::path& path,
                                                  const std::filesystem::path& audio_dir,
                                                  const DawProjectLocator& locate, std::string* error) {
    const auto file = read_dawproject(path, error);
    if (!file) return std::nullopt;
    Importer importer(*file, path, audio_dir, locate);
    if (!importer.run(error)) return std::nullopt;
    return std::move(importer.result);
}

} // namespace blokkily
