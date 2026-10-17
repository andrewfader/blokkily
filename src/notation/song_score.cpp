#include "blokkily/notation/song_score.hpp"

#include "blokkily/notation/pattern_bridge.hpp"

#include <algorithm>

namespace blokkily::notation {

namespace {

constexpr Tick kTicksPerBeat = 480;

std::vector<MeterChange> meters_of(const Song& song) {
    std::vector<MeterChange> meters;
    for (const auto& change : song.meter.changes) {
        try {
            meters.push_back({song.meter.bar_start(change.bar), TimeSignature(change.numerator, change.denominator)});
        } catch (const std::invalid_argument&) {
        }
    }
    if (meters.empty()) meters.push_back({0, TimeSignature(4, 4)});
    return meters;
}

KeySignature key_of(const Section& section) {
    return KeySignature(std::clamp(section.key_fifths, -7, 7));
}

Clef clef_named(const std::string& name) {
    for (int kind = 0; kind <= static_cast<int>(ClefKind::Percussion); ++kind)
        if (Clef(static_cast<ClefKind>(kind)).name() == name) return Clef(static_cast<ClefKind>(kind));
    return Clef(ClefKind::Treble);
}

// Nothing on the song's timeline yet: no notes and no recorded audio.
bool empty_song(const Song& song) {
    return song.audio_clips.empty() &&
           std::none_of(song.patterns.begin(), song.patterns.end(),
                        [](const PatternSlot& slot) { return !slot.pattern.events().empty(); });
}

// A track a score's part may take over: one that plays an instrument of its
// own. Another instrument's output, or a frozen track, keeps what it is.
bool lendable(const Track& track) { return !track.source && !track.frozen; }

// At most `count` characters of UTF-8 `text`, never cutting one in two.
std::string first_characters(const std::string& text, std::size_t count) {
    std::size_t end = 0;
    for (std::size_t seen = 0; end < text.size(); ++end) {
        if ((static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) continue; // inside a character
        if (seen++ == count) break;
    }
    return text.substr(0, end);
}

} // namespace

Pattern track_timeline(const Song& song, std::size_t track, Tick end) {
    Pattern timeline(std::max<Tick>(1, end), kTicksPerBeat);
    for (const auto& clip : song.clips) {
        if (clip.track != track || clip.pattern >= song.patterns.size()) continue;
        const Pattern& pattern = song.patterns[clip.pattern].pattern;
        const Tick length = pattern.length();
        if (length <= 0) continue;
        const Tick from = clip.start;
        const Tick to = clip.start + clip.span(length);
        // Pass k of the clip plays the pattern from start - offset + k*length.
        for (Tick pass = clip.start - clip.offset; pass < to; pass += length) {
            for (const Trigger& trigger : pattern.events()) {
                const Tick at = pass + trigger.start;
                if (at < from || at >= to || at >= timeline.length()) continue;
                Trigger placed = trigger;
                placed.id = 0;
                placed.start = at;
                placed.duration = std::min({trigger.duration, to - at, timeline.length() - at});
                (void)timeline.add(placed);
            }
        }
    }
    return timeline;
}

Score song_score(const Song& song, const std::string& title, std::size_t fallback_section) {
    Score score;
    score.title = title;
    if (!song.tempo.points.empty()) score.tempo_bpm = song.tempo.points.front().bpm;

    Tick end = 0;
    for (const auto& clip : song.clips)
        if (clip.pattern < song.patterns.size())
            end = std::max(end, clip.start + clip.span(song.patterns[clip.pattern].pattern.length()));

    if (end > 0) {
        const auto meters = meters_of(song);
        for (std::size_t track = 0; track < song.tracks.size(); ++track) {
            std::vector<const Clip*> clips;
            for (const auto& clip : song.clips)
                if (clip.track == track && clip.pattern < song.patterns.size()) clips.push_back(&clip);
            if (clips.empty()) continue;
            std::sort(clips.begin(), clips.end(), [](const Clip* a, const Clip* b) { return a->start < b->start; });
            const auto& first = song.patterns[clips.front()->pattern];
            const Section* first_section = first.section < song.sections.size() ? &song.sections[first.section] : nullptr;
            Staff staff = to_staff(track_timeline(song, track, end),
                                   first_section != nullptr ? key_of(*first_section) : KeySignature(0),
                                   clef_named(first.clef), meters);
            // A later section's key, or a part's clef, is written where its
            // clip starts.
            KeySignature key = staff.key;
            Clef clef = staff.clef;
            for (const Clip* clip : clips) {
                const auto& slot = song.patterns[clip->pattern];
                const auto bar = static_cast<std::size_t>(std::max(0, song.meter.bar_at(clip->start)));
                if (bar == 0 || bar >= staff.measures.size()) continue;
                if (slot.section < song.sections.size() && key_of(song.sections[slot.section]) != key) {
                    key = key_of(song.sections[slot.section]);
                    staff.measures[bar].key = key;
                }
                if (clef_named(slot.clef) != clef) {
                    clef = clef_named(slot.clef);
                    staff.measures[bar].clef = clef;
                }
            }
            score.parts.push_back({song.tracks[track].name, {std::move(staff)}});
        }
        return score;
    }

    // No arrangement yet: the one section, a part per track.
    if (fallback_section >= song.sections.size()) return score;
    const Section& section = song.sections[fallback_section];
    std::vector<MeterChange> meters = meters_of(song);
    if (section.unmetered) meters = {{0, TimeSignature::unmetered()}};
    else if (section.meter_beats > 0) meters = {{0, TimeSignature(section.meter_beats, section.meter_unit)}};
    for (std::size_t track = 0; track < song.tracks.size(); ++track) {
        const auto part = song.part(fallback_section, track);
        if (!part) continue;
        const auto& slot = song.patterns[*part];
        score.parts.push_back(
            {song.tracks[track].name, {to_staff(slot.pattern, key_of(section), clef_named(slot.clef), meters)}});
    }
    return score;
}

ScoreLanding add_score(Song& song, const Score& score) {
    ScoreLanding landing;
    landing.fresh = empty_song(song);

    // Every staff becomes a track's part, played back at the song's
    // resolution.
    struct Played {
        std::string name;
        const Staff* staff;
        Pattern pattern;
    };
    std::vector<Played> staves;
    Tick length = 1;
    for (const Part& part : score.parts)
        for (std::size_t s = 0; s < part.staves.size(); ++s) {
            int dropped = 0;
            Pattern pattern = to_pattern(part.staves[s], kTicksPerBeat, &dropped);
            landing.dropped += dropped;
            length = std::max(length, pattern.length());
            std::string name = part.name.empty() ? "Part " + std::to_string(staves.size() + 1) : part.name;
            if (part.staves.size() > 1) name += " " + std::to_string(s + 1);
            staves.push_back({name, &part.staves[s], std::move(pattern)});
        }
    if (staves.empty()) return landing;

    // Tracks: a song with nothing in it lends its own, in order.
    std::vector<std::size_t> lent;
    if (landing.fresh)
        for (std::size_t t = 0; t < song.tracks.size(); ++t)
            if (lendable(song.tracks[t])) lent.push_back(t);
    for (std::size_t i = 0; i < staves.size(); ++i) {
        std::size_t track = i < lent.size() ? lent[i] : 0;
        if (i >= lent.size()) {
            Track added;
            added.name = staves[i].name;
            song.tracks.push_back(std::move(added));
            track = song.tracks.size() - 1;
            song.add_track_parts(track);
        } else {
            song.tracks[track].name = staves[i].name;
        }
        landing.tracks.push_back(track);
    }

    const Staff& first = *staves.front().staff;
    landing.section = song.add_section(score.title.empty() ? "SCORE" : first_characters(score.title, 24), length, kTicksPerBeat);
    Section& section = song.sections[landing.section];
    section.key_fifths = first.key.fifths();
    section.unmetered = !first.time.metered();
    if (first.time.metered()) {
        section.meter_beats = first.time.beats();
        section.meter_unit = first.time.beat_unit();
    }
    for (std::size_t i = 0; i < staves.size(); ++i) {
        const auto part = song.part(landing.section, landing.tracks[i]);
        if (!part) continue;
        auto& slot = song.patterns[*part];
        slot.clef = std::string(staves[i].staff->clef.name());
        for (const Trigger& trigger : staves[i].pattern.events()) {
            Trigger note = trigger;
            note.id = 0;
            if (slot.pattern.add(note) != 0) ++landing.notes;
        }
    }

    if (landing.fresh) {
        if (first.time.metered())
            song.meter.changes = {blokkily::MeterChange{0, static_cast<std::int16_t>(first.time.beats()),
                                                        static_cast<std::int16_t>(first.time.beat_unit())}};
        if (score.tempo_bpm && *score.tempo_bpm >= minimum_bpm && *score.tempo_bpm <= maximum_bpm)
            song.tempo.points = {TempoPoint{0, *score.tempo_bpm, false}};
    }

    // Its clips go after the arrangement's last, audio included.
    Tick after = song.clips.empty() && song.audio_clips.empty() ? 0 : song.length();
    // A song with no notes had nothing worth keeping on its timeline.
    if (landing.fresh) {
        after = 0;
        song.clips.clear();
    }
    const auto bar = song.meter.bar_at(after) + (song.meter.bar_start(song.meter.bar_at(after)) < after ? 1 : 0);
    landing.start = song.meter.bar_start(bar);
    (void)song.place_section(landing.section, landing.start);
    return landing;
}

} // namespace blokkily::notation
