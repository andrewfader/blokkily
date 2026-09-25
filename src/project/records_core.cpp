// The records format 4 defined. Their shape is frozen: later features add
// records of their own (plan §F-F) rather than extending these lines.

#include "records.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace blokkily::project_io {

Trigger* PatternDraft::find(EventId id) {
    const auto it = std::find_if(triggers.begin(), triggers.end(),
        [id](const Trigger& trigger) { return trigger.id == id; });
    return it == triggers.end() ? nullptr : &*it;
}

namespace {

void write_core(const Project& project, WriteContext& context) {
    auto& out = context.out;
    out << "name " << escape(project.name) << '\n';
    out << "master " << number(project.song.master_gain_db) << '\n';
    // The tuning travels in full rather than by name, so a session written in a
    // scale this build has never heard of still reloads as itself.
    out << "tuning " << escape(project.song.tuning.name) << ' '
        << number(project.song.tuning.period_cents) << ' '
        << project.song.tuning.anchor_key << ' '
        << project.song.tuning.degrees.size();
    for (const double cents : project.song.tuning.degrees) out << ' ' << number(cents);
    out << '\n';
    out << "scale " << escape(project.song.scale.name) << ' '
        << project.song.scale.cents.size();
    for (const double cents : project.song.scale.cents) out << ' ' << number(cents);
    out << '\n';
    out << "harmony " << project.song.root_degree << ' '
        << (project.song.auto_scale ? 1 : 0) << '\n';
    for (std::size_t index = 0; index < project.song.patterns.size(); ++index) {
        const auto& slot = project.song.patterns[index];
        out << "pattern " << escape(slot.name) << ' ' << slot.pattern.length() << ' '
            << slot.pattern.ticks_per_beat() << '\n';
        for (const auto& trigger : slot.pattern.events()) {
            out << "trigger " << index << ' ' << trigger.id << ' ' << trigger.start << ' '
                << trigger.duration << ' ' << trigger.micro_offset << ' '
                << number(trigger.probability) << ' '
                << static_cast<unsigned>(trigger.ratchets) << ' '
                << static_cast<unsigned>(trigger.play_on_loop);
            if (const auto* note = std::get_if<Note>(&trigger.musical_data)) {
                out << " note " << note->key << ' ' << number(note->velocity) << ' '
                    << number(note->release_velocity) << ' ' << number(note->cents);
            } else {
                const auto& chord = std::get<Chord>(trigger.musical_data);
                out << " chord " << chord.root << ' ' << static_cast<int>(chord.inversion) << ' '
                    << chord.strum << ' ' << chord.intervals.size();
                for (const auto interval : chord.intervals) out << ' ' << interval;
                // One retune per interval, always written, so a chord reads
                // back in the tuning it was played in.
                for (std::size_t voice = 0; voice < chord.intervals.size(); ++voice)
                    out << ' ' << number(voice < chord.cents.size() ? chord.cents[voice] : 0.0);
            }
            out << '\n';
            for (const auto& lock : trigger.locks)
                out << "lock " << index << ' ' << trigger.id << ' ' << escape(lock.parameter_id)
                    << ' ' << lock.parameter_index << ' '
                    << (lock.kind == ParameterLock::Kind::modulation ? "modulation" : "automation")
                    << ' ' << number(lock.value) << '\n';
        }
    }
    for (const auto& track : project.song.tracks)
        out << "track " << escape(track.name) << ' ' << number(track.mix.gain_db) << ' '
            << number(track.mix.pan) << ' ' << (track.mix.mute ? 1 : 0) << ' '
            << (track.mix.solo ? 1 : 0) << ' ' << escape(track.instrument.format) << ' '
            << escape(track.instrument.path) << ' ' << escape(track.instrument.identifier)
            << ' ' << encode_base64(track.instrument.state) << '\n';
    for (const auto& clip : project.song.clips)
        out << "clip " << clip.track << ' ' << clip.pattern << ' ' << clip.start << ' '
            << clip.repeats << '\n';
}

bool parse_name(const Fields& fields, ParseContext& context) {
    const auto value = fields.text(1);
    if (!fields.count(2) || !value) return context.fail("malformed name record");
    context.project.name = *value;
    return true;
}

// `tempo <bpm>` is the legacy single tempo, accepted in every format version
// and never written: the timebase module turns it into one tempo point at
// tick 0 (plan C3).
bool parse_tempo(const Fields& fields, ParseContext& context) {
    const auto value = fields.real(1);
    if (!fields.count(2) || !value || !std::isfinite(*value) || *value <= 0.0)
        return context.fail("malformed tempo record");
    if (context.legacy_tempo) return context.fail("more than one tempo record");
    context.legacy_tempo = *value;
    return true;
}

bool parse_master(const Fields& fields, ParseContext& context) {
    const auto value = fields.real(1);
    if (!fields.count(2) || !value) return context.fail("malformed master record");
    context.project.song.master_gain_db = *value;
    return true;
}

bool parse_tuning(const Fields& fields, ParseContext& context) {
    const auto name = fields.text(1);
    const auto period = fields.real(2);
    const auto anchor = fields.integer(3);
    const auto count = fields.integer(4);
    if (!name || !period || !anchor || !count || *period <= 0.0 || *count <= 0 ||
        !fields.count(static_cast<std::size_t>(5 + *count)))
        return context.fail("malformed tuning record");
    Tuning tuning;
    tuning.name = *name;
    tuning.period_cents = *period;
    tuning.anchor_key = static_cast<int>(*anchor);
    tuning.degrees.clear();
    for (int degree = 0; degree < *count; ++degree) {
        const auto cents = fields.real(static_cast<std::size_t>(5 + degree));
        if (!cents) return context.fail("malformed tuning record");
        tuning.degrees.push_back(*cents);
    }
    context.project.song.tuning = std::move(tuning);
    return true;
}

bool parse_scale(const Fields& fields, ParseContext& context) {
    const auto name = fields.text(1);
    const auto count = fields.integer(2);
    if (!name || !count || *count < 0 || !fields.count(static_cast<std::size_t>(3 + *count)))
        return context.fail("malformed scale record");
    Scale scale;
    scale.name = *name;
    for (int degree = 0; degree < *count; ++degree) {
        const auto cents = fields.real(static_cast<std::size_t>(3 + degree));
        if (!cents) return context.fail("malformed scale record");
        scale.cents.push_back(*cents);
    }
    context.project.song.scale = std::move(scale);
    return true;
}

bool parse_harmony(const Fields& fields, ParseContext& context) {
    const auto root = fields.integer(1);
    const auto automatic = fields.integer(2);
    if (!fields.count(3) || !root || !automatic) return context.fail("malformed harmony record");
    context.project.song.root_degree = static_cast<int>(*root);
    context.project.song.auto_scale = *automatic != 0;
    return true;
}

bool parse_pattern(const Fields& fields, ParseContext& context) {
    const auto name = fields.text(1);
    const auto length = fields.integer(2);
    const auto ticks = fields.integer(3);
    if (!fields.count(4) || !name || !length || !ticks || *length <= 0 || *ticks <= 0)
        return context.fail("malformed pattern record");
    context.drafts.push_back({*name, *length, *ticks, {}});
    return true;
}

bool parse_trigger(const Fields& fields, ParseContext& context) {
    if (!fields.at_least(10)) return context.fail("malformed trigger record");
    const auto pattern_index = fields.integer(1);
    const auto id = fields.integer(2);
    const auto start = fields.integer(3);
    const auto duration = fields.integer(4);
    const auto micro = fields.integer(5);
    const auto probability = fields.real(6);
    const auto ratchets = fields.integer(7);
    const auto loop = fields.integer(8);
    if (!pattern_index || !id || !start || !duration || !micro || !probability ||
        !ratchets || !loop || *id <= 0 || *pattern_index < 0 ||
        static_cast<std::size_t>(*pattern_index) >= context.drafts.size() ||
        *ratchets < 0 || *ratchets > 255 || *loop < 0 || *loop > 255)
        return context.fail("malformed trigger record");
    Trigger trigger;
    trigger.id = static_cast<EventId>(*id);
    trigger.start = *start;
    trigger.duration = *duration;
    trigger.micro_offset = *micro;
    trigger.probability = static_cast<float>(*probability);
    trigger.ratchets = static_cast<std::uint8_t>(*ratchets);
    trigger.play_on_loop = static_cast<std::uint8_t>(*loop);

    if (fields.tokens[9] == "note") {
        const auto key = fields.integer(10);
        const auto velocity = fields.real(11);
        const auto release = fields.real(12);
        const auto cents = fields.real(13);
        if (!fields.count(14) || !key || !velocity || !release || !cents)
            return context.fail("malformed note payload");
        trigger.musical_data = Note{static_cast<std::int16_t>(*key),
                                    static_cast<float>(*velocity),
                                    static_cast<float>(*release), *cents};
    } else if (fields.tokens[9] == "chord") {
        const auto root = fields.integer(10);
        const auto inversion = fields.integer(11);
        const auto strum = fields.integer(12);
        const auto count = fields.integer(13);
        if (!root || !inversion || !strum || !count || *count < 0 ||
            !fields.count(14 + 2 * static_cast<std::size_t>(*count)))
            return context.fail("malformed chord payload");
        Chord chord;
        chord.root = static_cast<std::int16_t>(*root);
        chord.inversion = static_cast<std::int8_t>(*inversion);
        chord.strum = *strum;
        chord.intervals.clear();
        for (long long index = 0; index < *count; ++index) {
            const auto interval = fields.integer(14 + static_cast<std::size_t>(index));
            if (!interval) return context.fail("malformed chord interval");
            chord.intervals.push_back(static_cast<std::int16_t>(*interval));
        }
        chord.cents.clear();
        for (long long index = 0; index < *count; ++index) {
            const auto retune = fields.real(14 + static_cast<std::size_t>(*count + index));
            if (!retune) return context.fail("malformed chord retune");
            chord.cents.push_back(*retune);
        }
        trigger.musical_data = chord;
    } else {
        return context.fail("unknown musical payload");
    }
    auto& draft = context.drafts[static_cast<std::size_t>(*pattern_index)];
    if (draft.find(trigger.id) != nullptr) return context.fail("duplicate trigger identifier");
    draft.triggers.push_back(std::move(trigger));
    return true;
}

bool parse_lock(const Fields& fields, ParseContext& context) {
    const auto pattern_index = fields.integer(1);
    const auto id = fields.integer(2);
    const auto parameter = fields.text(3);
    const auto index = fields.integer(4);
    const auto value = fields.real(6);
    if (!fields.count(7) || !pattern_index || !id || !parameter || !index || !value ||
        *pattern_index < 0 || static_cast<std::size_t>(*pattern_index) >= context.drafts.size() ||
        *index < 0 || *index > 0x7FFFFFFF)
        return context.fail("malformed lock record");
    const std::string& kind_token = fields.tokens[5];
    if (kind_token != "automation" && kind_token != "modulation")
        return context.fail("unknown lock kind");
    auto* trigger = context.drafts[static_cast<std::size_t>(*pattern_index)].find(
        static_cast<EventId>(*id));
    if (trigger == nullptr) return context.fail("lock refers to an unknown trigger");
    trigger->locks.push_back({*parameter, static_cast<std::int32_t>(*index), *value,
                              kind_token == "modulation" ? ParameterLock::Kind::modulation
                                                         : ParameterLock::Kind::automation});
    return true;
}

bool parse_track(const Fields& fields, ParseContext& context) {
    const auto name = fields.text(1);
    const auto gain = fields.real(2);
    const auto pan = fields.real(3);
    const auto mute = fields.integer(4);
    const auto solo = fields.integer(5);
    const auto format = fields.text(6);
    const auto path = fields.text(7);
    const auto identifier = fields.text(8);
    if (!fields.count(10) || !name || !gain || !pan || !mute || !solo || !format || !path ||
        !identifier || *mute < 0 || *mute > 1 || *solo < 0 || *solo > 1)
        return context.fail("malformed track record");
    auto state = decode_base64(fields.tokens[9]);
    if (!state) return context.fail("malformed instrument state");
    Track track;
    track.name = *name;
    track.mix = {*gain, *pan, *mute == 1, *solo == 1};
    track.instrument = {*format, *path, *identifier, std::move(*state)};
    context.tracks.push_back(std::move(track));
    return true;
}

bool parse_clip(const Fields& fields, ParseContext& context) {
    const auto track = fields.integer(1);
    const auto pattern_index = fields.integer(2);
    const auto start = fields.integer(3);
    const auto repeats = fields.integer(4);
    if (!fields.count(5) || !track || !pattern_index || !start || !repeats || *track < 0 ||
        *pattern_index < 0 || *start < 0 || *repeats <= 0 || *repeats > 0xFFFF)
        return context.fail("malformed clip record");
    context.clips.push_back({static_cast<std::size_t>(*track),
                             static_cast<std::size_t>(*pattern_index), *start,
                             static_cast<std::uint32_t>(*repeats)});
    return true;
}

// Moves the drafts, tracks and clips into the song. Consistency of the whole
// song is checked by ProjectFile::parse after every module has finished.
bool finish_core(ParseContext& context) {
    if (context.drafts.empty()) return context.fail("project has no pattern record");
    if (context.tracks.empty()) return context.fail("project has no track record");

    auto& song = context.project.song;
    song.patterns.clear();
    for (auto& draft : context.drafts) {
        PatternSlot slot;
        slot.name = draft.name;
        try {
            slot.pattern = Pattern(draft.length, draft.ticks_per_beat);
            for (auto& trigger : draft.triggers)
                if (!slot.pattern.restore(std::move(trigger)))
                    return context.fail("trigger could not be restored");
        } catch (const std::invalid_argument& thrown) {
            return context.fail(thrown.what());
        }
        song.patterns.push_back(std::move(slot));
    }
    song.tracks = std::move(context.tracks);
    song.clips = std::move(context.clips);
    context.drafts.clear();
    context.tracks.clear();
    context.clips.clear();
    return true;
}

constexpr std::array core_handlers{
    RecordHandler{"name", parse_name},
    RecordHandler{"tempo", parse_tempo},
    RecordHandler{"master", parse_master},
    RecordHandler{"tuning", parse_tuning},
    RecordHandler{"scale", parse_scale},
    RecordHandler{"harmony", parse_harmony},
    RecordHandler{"pattern", parse_pattern},
    RecordHandler{"trigger", parse_trigger},
    RecordHandler{"lock", parse_lock},
    RecordHandler{"track", parse_track},
    RecordHandler{"clip", parse_clip},
};

} // namespace

const RecordModule& core_records() {
    static const RecordModule module{"core", write_core, core_handlers, finish_core};
    return module;
}

} // namespace blokkily::project_io
