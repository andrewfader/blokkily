// MusicXML 4.0 partwise import/export for blokkily::notation scores.
//
// API-adapter note: every assumption about task A's headers
// (docs/notation-design.md: Step, Accidental, SpelledPitch, Clef, KeySignature,
// TimeSignature, NoteValue, NotationNote, Rest, Measure, Staff, Part, Score)
// is isolated in the "API adapter" section below. If the landed headers name
// things differently, only that section needs to change; the MusicXML logic
// underneath is independent of it.

#include "blokkily/notation/musicxml.hpp"

#include "pugixml.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace blokkily::notation {
namespace {

// ---------------------------------------------------------------------------
// API adapter (assumptions from docs/notation-design.md)
// ---------------------------------------------------------------------------

// Ticks per quarter note used for <divisions>. 3840 keeps every duration in
// the NoteValue enum (breve..128th) with up to three dots an exact integer:
// the shortest unit, a triple-dotted 128th, is 3840/32*1.875 = 225 divisions.
constexpr int kDivisions = 3840;

int accidental_value(Accidental accidental) { return static_cast<int>(accidental); }

char step_letter(Step step) {
    switch (step) {
        case Step::C: return 'C';
        case Step::D: return 'D';
        case Step::E: return 'E';
        case Step::F: return 'F';
        case Step::G: return 'G';
        case Step::A: return 'A';
        case Step::B: return 'B';
    }
    throw std::runtime_error("musicxml: unknown Step value");
}

Step step_from_letter(char letter) {
    switch (letter) {
        case 'C': return Step::C;
        case 'D': return Step::D;
        case 'E': return Step::E;
        case 'F': return Step::F;
        case 'G': return Step::G;
        case 'A': return Step::A;
        case 'B': return Step::B;
        default: break;
    }
    throw std::runtime_error(std::string("musicxml: bad step letter '") + letter + "'");
}

const char* note_type_name(NoteValue value) {
    switch (value) {
        case NoteValue::Breve: return "breve";
        case NoteValue::Whole: return "whole";
        case NoteValue::Half: return "half";
        case NoteValue::Quarter: return "quarter";
        case NoteValue::Eighth: return "eighth";
        case NoteValue::Sixteenth: return "16th";
        case NoteValue::ThirtySecond: return "32nd";
        case NoteValue::SixtyFourth: return "64th";
        case NoteValue::HundredTwentyEighth: return "128th";
    }
    throw std::runtime_error("musicxml: unknown NoteValue");
}

NoteValue note_value_from_type(const std::string& type) {
    if (type == "breve") return NoteValue::Breve;
    if (type == "whole") return NoteValue::Whole;
    if (type == "half") return NoteValue::Half;
    if (type == "quarter") return NoteValue::Quarter;
    if (type == "eighth") return NoteValue::Eighth;
    if (type == "16th") return NoteValue::Sixteenth;
    if (type == "32nd") return NoteValue::ThirtySecond;
    if (type == "64th") return NoteValue::SixtyFourth;
    if (type == "128th") return NoteValue::HundredTwentyEighth;
    throw std::runtime_error("musicxml: unsupported <type> '" + type + "'");
}

// Duration in divisions, using the model's own tick conversion so the wire
// format and the engine always agree.
long long duration_divisions(NoteDuration duration) {
    return ticks(duration, kDivisions);
}

// Clef to MusicXML (sign, line); line 0 means "no <line> element" (percussion).
std::pair<std::string, int> clef_sign_line(const Clef& clef) {
    switch (clef.kind()) {
        case ClefKind::FrenchViolin: return {"G", 1};
        case ClefKind::Treble: return {"G", 2};
        case ClefKind::Subbass: return {"F", 5};
        case ClefKind::Bass: return {"F", 4};
        case ClefKind::BaritoneF: return {"F", 3};
        case ClefKind::Soprano: return {"C", 1};
        case ClefKind::MezzoSoprano: return {"C", 2};
        case ClefKind::Alto: return {"C", 3};
        case ClefKind::Tenor: return {"C", 4};
        case ClefKind::BaritoneC: return {"C", 5};
        case ClefKind::Percussion: return {"percussion", 0};
    }
    throw std::runtime_error("musicxml: unknown Clef value");
}

// Unknown clef signs keep the staff's current clef (import never fails on
// notation it does not understand); the common cases map exactly.
bool clef_from_sign_line(const std::string& sign, int line, Clef& out) {
    if (sign == "G" && line == 1) out = Clef(ClefKind::FrenchViolin);
    else if (sign == "G") out = Clef(ClefKind::Treble);
    else if (sign == "F" && line == 5) out = Clef(ClefKind::Subbass);
    else if (sign == "F" && line == 3) out = Clef(ClefKind::BaritoneF);
    else if (sign == "F") out = Clef(ClefKind::Bass);
    else if (sign == "C" && line == 1) out = Clef(ClefKind::Soprano);
    else if (sign == "C" && line == 2) out = Clef(ClefKind::MezzoSoprano);
    else if (sign == "C" && line == 4) out = Clef(ClefKind::Tenor);
    else if (sign == "C" && line == 5) out = Clef(ClefKind::BaritoneC);
    else if (sign == "C") out = Clef(ClefKind::Alto);
    else if (sign == "percussion") out = Clef(ClefKind::Percussion);
    else return false;
    return true;
}

Staff default_staff() {
    Staff staff;
    staff.clef = Clef(ClefKind::Treble);
    staff.key = KeySignature(0);
    staff.time = TimeSignature(4, 4);
    staff.transpose_octaves = 0;
    return staff;
}

// ---------------------------------------------------------------------------
// Small XML helpers (pugixml escapes text/attributes, so no manual escaping)
// ---------------------------------------------------------------------------

void append_text(pugi::xml_node parent, const char* name, const std::string& text) {
    auto node = parent.append_child(name);
    node.append_child(pugi::node_pcdata).set_value(text.c_str());
}

void append_int(pugi::xml_node parent, const char* name, long long value) {
    append_text(parent, name, std::to_string(value));
}

int attr_number_or(const pugi::xml_node& node, int fallback) {
    const auto attr = node.attribute("number");
    return attr ? attr.as_int(fallback) : fallback;
}

// ---------------------------------------------------------------------------
// Lyrics
// ---------------------------------------------------------------------------
// Model markers: a plain syllable, "-" (melisma continuation), "_" (extender
// line after the previous syllable). Export turns a run of
// text,"-","-",... into syllabic begin/middle/end with <extend/> on the
// continuations; "_" attaches <extend/> to the previous lyric-bearing note.
// Import inverts this: text -> the syllable, text-less lyric -> "-",
// a lyric-less note following a text+extend lyric -> "_".

struct LyricOut {
    bool emit = false;
    std::string syllabic = "single";
    std::string text;
    bool extend = false;
};

void plan_lyric_segment(const std::vector<StaffObject>& events,
                        const std::vector<std::size_t>& indices,
                        std::vector<LyricOut>& out) {
    const auto lyric_at = [&](std::size_t event_index) -> const std::string& {
        return std::get<NotationNote>(events[event_index]).lyric;
    };
    // Pass 1: "_" extenders attach to the previous lyric-bearing note.
    std::vector<int> extended(indices.size(), 0);
    int last_lyric = -1;
    for (std::size_t k = 0; k < indices.size(); ++k) {
        const auto& lyric = lyric_at(indices[k]);
        if (lyric == "_") {
            if (last_lyric >= 0) extended[static_cast<std::size_t>(last_lyric)] = 1;
        } else if (!lyric.empty()) {
            last_lyric = static_cast<int>(k);
        }
    }
    // Pass 2: runs of text followed by "-" continuations.
    std::size_t k = 0;
    while (k < indices.size()) {
        const auto& head = lyric_at(indices[k]);
        if (head.empty() || head == "_") {
            ++k;
            continue;
        }
        std::size_t j = k;
        while (j + 1 < indices.size() && lyric_at(indices[j + 1]) == "-") ++j;
        const bool has_text = (head != "-");
        for (std::size_t m = k; m <= j; ++m) {
            auto& plan = out[indices[m]];
            plan.emit = true;
            if (m == k) {
                plan.syllabic = (j > k && has_text) ? "begin" : "single";
                if (has_text) plan.text = head;
            } else {
                plan.syllabic = (m == j) ? "end" : "middle";
            }
            if (m > k || !has_text) plan.extend = true;
            if (extended[m] != 0) plan.extend = true;
        }
        k = j + 1;
    }
}

// Lyrics never cross rests or voice/staff boundaries: plan per group, with
// rests splitting the group into segments.
std::vector<LyricOut> plan_lyrics(const std::vector<StaffObject>& events) {
    std::vector<LyricOut> out(events.size());
    std::vector<std::size_t> segment;
    const auto flush = [&] {
        if (!segment.empty()) plan_lyric_segment(events, segment, out);
        segment.clear();
    };
    for (std::size_t i = 0; i < events.size(); ++i) {
        if (std::holds_alternative<Rest>(events[i])) {
            flush();
        } else {
            segment.push_back(i);
        }
    }
    flush();
    return out;
}

// ---------------------------------------------------------------------------
// Note writing
// ---------------------------------------------------------------------------

void write_pitched_note(pugi::xml_node parent, const NotationNote& note, bool percussion,
                        bool multi_staff, int staff_number, const LyricOut& lyric) {
    auto xml_note = parent.append_child("note");
    if (percussion) {
        auto unpitched = xml_note.append_child("unpitched");
        append_text(unpitched, "display-step",
                    std::string(1, step_letter(note.spelled.step)));
        append_int(unpitched, "display-octave", note.spelled.octave);
    } else {
        auto pitch = xml_note.append_child("pitch");
        append_text(pitch, "step", std::string(1, step_letter(note.spelled.step)));
        const int alter = accidental_value(note.spelled.accidental);
        if (alter != 0) append_int(pitch, "alter", alter);
        append_int(pitch, "octave", note.spelled.octave);
    }
    append_int(xml_note, "duration", duration_divisions(note.duration));
    if (note.tie_start) {
        auto tie = xml_note.append_child("tie");
        tie.append_attribute("type") = "start";
    }
    if (note.tie_end) {
        auto tie = xml_note.append_child("tie");
        tie.append_attribute("type") = "stop";
    }
    append_int(xml_note, "voice", note.voice);
    append_text(xml_note, "type", note_type_name(note.duration.value));
    for (int d = 0; d < note.duration.dots; ++d) xml_note.append_child("dot");
    if (note.duration.stemless) append_text(xml_note, "stem", "none");
    if (multi_staff) append_int(xml_note, "staff", staff_number);
    if (note.tie_start || note.tie_end) {
        auto notations = xml_note.append_child("notations");
        if (note.tie_start) {
            auto tied = notations.append_child("tied");
            tied.append_attribute("type") = "start";
        }
        if (note.tie_end) {
            auto tied = notations.append_child("tied");
            tied.append_attribute("type") = "stop";
        }
    }
    if (lyric.emit) {
        auto xml_lyric = xml_note.append_child("lyric");
        append_text(xml_lyric, "syllabic", lyric.syllabic);
        if (!lyric.text.empty()) append_text(xml_lyric, "text", lyric.text);
        if (lyric.extend) xml_lyric.append_child("extend");
    }
}

void write_rest(pugi::xml_node parent, const Rest& rest, int voice, bool multi_staff,
                int staff_number) {
    auto xml_note = parent.append_child("note");
    xml_note.append_child("rest");
    append_int(xml_note, "duration", duration_divisions(rest.duration));
    append_int(xml_note, "voice", voice);
    append_text(xml_note, "type", note_type_name(rest.duration.value));
    for (int d = 0; d < rest.duration.dots; ++d) xml_note.append_child("dot");
    if (multi_staff) append_int(xml_note, "staff", staff_number);
}

// ---------------------------------------------------------------------------
// Note reading
// ---------------------------------------------------------------------------

bool has_tie(const pugi::xml_node& note, const char* kind) {
    for (auto tie : note.children("tie")) {
        if (std::string(tie.attribute("type").value()) == kind) return true;
    }
    if (const auto notations = note.child("notations")) {
        for (auto tied : notations.children("tied")) {
            if (std::string(tied.attribute("type").value()) == kind) return true;
        }
    }
    return false;
}

// Best NoteDuration match for a raw duration; exact matches win, otherwise
// the nearest combo (deterministic: longest value, fewest dots on ties).
NoteDuration nearest_duration(long long divisions, int unit) {
    static const NoteValue values[] = {
        NoteValue::Breve, NoteValue::Whole, NoteValue::Half, NoteValue::Quarter,
        NoteValue::Eighth, NoteValue::Sixteenth, NoteValue::ThirtySecond,
        NoteValue::SixtyFourth, NoteValue::HundredTwentyEighth,
    };
    NoteDuration best{NoteValue::Quarter, 0};
    long long best_diff = -1;
    for (const auto value : values) {
        for (int dots = 0; dots <= 3; ++dots) {
            const NoteDuration candidate{value, dots};
            const long long candidate_ticks = ticks(candidate, unit);
            const long long diff = std::llabs(candidate_ticks - divisions);
            if (best_diff < 0 || diff < best_diff) {
                best = candidate;
                best_diff = diff;
            }
        }
    }
    return best;
}

} // namespace

// ---------------------------------------------------------------------------
// write
// ---------------------------------------------------------------------------

std::string write(const Score& score) {
    pugi::xml_document doc;
    auto declaration = doc.append_child(pugi::node_declaration);
    declaration.append_attribute("version") = "1.0";
    declaration.append_attribute("encoding") = "UTF-8";
    auto doctype = doc.append_child(pugi::node_doctype);
    doctype.set_value(
        "score-partwise PUBLIC "
        "\"-//Recordare//DTD MusicXML 4.0 Partwise//EN\" "
        "\"http://www.musicxml.com/dtds/partwise.dtd\"");

    auto root = doc.append_child("score-partwise");
    root.append_attribute("version") = "4.0";

    if (!score.title.empty()) {
        auto work = root.append_child("work");
        append_text(work, "work-title", score.title);
    }
    if (!score.composer.empty()) {
        auto identification = root.append_child("identification");
        auto creator = identification.append_child("creator");
        creator.append_attribute("type") = "composer";
        creator.append_child(pugi::node_pcdata).set_value(score.composer.c_str());
    }

    auto part_list = root.append_child("part-list");
    for (std::size_t p = 0; p < score.parts.size(); ++p) {
        auto score_part = part_list.append_child("score-part");
        score_part.append_attribute("id") = ("P" + std::to_string(p + 1)).c_str();
        append_text(score_part, "part-name", score.parts[p].name);
    }

    for (std::size_t p = 0; p < score.parts.size(); ++p) {
        const Part& part = score.parts[p];
        auto xml_part = root.append_child("part");
        xml_part.append_attribute("id") = ("P" + std::to_string(p + 1)).c_str();
        const bool multi_staff = part.staves.size() > 1;

        std::size_t measure_count = 0;
        for (const auto& staff : part.staves)
            measure_count = std::max(measure_count, staff.measures.size());
        if (measure_count == 0) measure_count = 1; // keep the part valid

        const bool unmetered_part =
            !part.staves.empty() &&
            std::all_of(part.staves.begin(), part.staves.end(),
                        [](const Staff& s) { return !(s.time.metered()); });

        for (std::size_t m = 0; m < measure_count; ++m) {
            auto xml_measure = xml_part.append_child("measure");
            xml_measure.append_attribute("number") = std::to_string(m + 1).c_str();

            if (m == 0) {
                auto attributes = xml_measure.append_child("attributes");
                append_int(attributes, "divisions", kDivisions);
                for (std::size_t s = 0; s < part.staves.size(); ++s) {
                    const Staff& staff = part.staves[s];
                    const int number = static_cast<int>(s + 1);
                    auto key = attributes.append_child("key");
                    if (multi_staff) key.append_attribute("number") = number;
                    append_int(key, "fifths", staff.key.fifths());
                    auto time = attributes.append_child("time");
                    if (multi_staff) time.append_attribute("number") = number;
                    if (!staff.time.metered()) {
                        time.append_attribute("senza-misura") = "1";
                    } else {
                        append_int(time, "beats", staff.time.beats());
                        append_int(time, "beat-type", staff.time.beat_unit());
                    }
                }
                if (multi_staff)
                    append_int(attributes, "staves",
                               static_cast<long long>(part.staves.size()));
                for (std::size_t s = 0; s < part.staves.size(); ++s) {
                    const Staff& staff = part.staves[s];
                    const int number = static_cast<int>(s + 1);
                    const auto [sign, line] = clef_sign_line(staff.clef);
                    auto clef = attributes.append_child("clef");
                    if (multi_staff) clef.append_attribute("number") = number;
                    append_text(clef, "sign", sign);
                    if (line > 0) append_int(clef, "line", line);
                    if (staff.transpose_octaves != 0) {
                        auto transpose = attributes.append_child("transpose");
                        if (multi_staff) transpose.append_attribute("number") = number;
                        append_int(transpose, "diatonic", 0);
                        append_int(transpose, "chromatic", 0);
                        append_int(transpose, "octave-change", staff.transpose_octaves);
                    }
                }
            }

            // One staff group after another, rewound with <backup> for grand
            // staves. Events keep the model's flat time order inside a staff:
            // the model is a single timeline per staff (to_pattern walks it
            // sequentially and ignores voice for timing), so regrouping by
            // voice would move notes in time and break the round trip. Voice
            // survives as a label on each note.
            struct Group {
                int staff_number;
                std::vector<StaffObject> events;
            };
            std::vector<Group> groups;
            for (std::size_t s = 0; s < part.staves.size(); ++s) {
                const Staff& staff = part.staves[s];
                if (m >= staff.measures.size()) continue;
                auto it = std::find_if(groups.begin(), groups.end(),
                                       [&](const Group& g) {
                                           return g.staff_number ==
                                                  static_cast<int>(s + 1);
                                       });
                if (it == groups.end()) {
                    groups.push_back({static_cast<int>(s + 1), {}});
                    it = groups.end() - 1;
                }
                for (const auto& event : staff.measures[m].objects)
                    it->events.push_back(event);
            }

            long long previous_total = 0;
            bool first_group = true;
            for (const auto& group : groups) {
                if (!first_group && previous_total > 0) {
                    auto backup = xml_measure.append_child("backup");
                    append_int(backup, "duration", previous_total);
                }
                first_group = false;
                previous_total = 0;
                const bool percussion =
                    group.staff_number >= 1 &&
                    static_cast<std::size_t>(group.staff_number - 1) <
                        part.staves.size() &&
                    !part.staves[static_cast<std::size_t>(group.staff_number - 1)]
                             .clef.pitched();
                const auto lyrics = plan_lyrics(group.events);
                for (std::size_t e = 0; e < group.events.size(); ++e) {
                    const auto& event = group.events[e];
                    if (const auto* rest = std::get_if<Rest>(&event)) {
                        write_rest(xml_measure, *rest, 1, multi_staff,
                                   group.staff_number);
                        previous_total += duration_divisions(rest->duration);
                    } else {
                        const auto& note = std::get<NotationNote>(event);
                        write_pitched_note(xml_measure, note, percussion, multi_staff,
                                           group.staff_number, lyrics[e]);
                        previous_total += duration_divisions(note.duration);
                    }
                }
            }

            if (m + 1 == measure_count && !unmetered_part) {
                auto barline = xml_measure.append_child("barline");
                barline.append_attribute("location") = "right";
                append_text(barline, "bar-style", "light-heavy");
            }
        }
    }

    std::ostringstream out;
    doc.save(out, "  ", pugi::format_default, pugi::encoding_utf8);
    return out.str();
}

// ---------------------------------------------------------------------------
// parse
// ---------------------------------------------------------------------------

Score parse(const std::string& xml) {
    pugi::xml_document doc;
    const auto result = doc.load_string(xml.c_str());
    if (!result)
        throw std::runtime_error(std::string("musicxml: parse error: ") +
                                 result.description());
    const auto root = doc.child("score-partwise");
    if (!root)
        throw std::runtime_error(
            "musicxml: expected a <score-partwise> document (timewise is not "
            "supported in v1)");

    Score score;
    score.title = root.child("work").child_value("work-title");
    if (const auto identification = root.child("identification")) {
        for (const auto creator : identification.children("creator")) {
            if (std::string(creator.attribute("type").value()) == "composer") {
                score.composer = creator.child_value();
                break;
            }
        }
    }

    // Part order follows the part list; parts missing from it are appended.
    std::map<std::string, std::size_t> id_to_index;
    if (const auto part_list = root.child("part-list")) {
        for (const auto score_part : part_list.children("score-part")) {
            const std::string id = score_part.attribute("id").value();
            Part part;
            part.name = score_part.child_value("part-name");
            id_to_index[id] = score.parts.size();
            score.parts.push_back(std::move(part));
        }
    }

    for (const auto xml_part : root.children("part")) {
        const std::string id = xml_part.attribute("id").value();
        std::size_t part_index;
        const auto found = id_to_index.find(id);
        if (found == id_to_index.end()) {
            part_index = score.parts.size();
            id_to_index[id] = part_index;
            score.parts.push_back(Part{});
        } else {
            part_index = found->second;
        }
        Part& part = score.parts[part_index];

        const auto ensure_staff = [&](std::size_t index) {
            while (part.staves.size() <= index)
                part.staves.push_back(default_staff());
        };
        ensure_staff(0);

        int divisions = kDivisions;
        std::size_t measure_index = 0;
        for (const auto xml_measure : xml_part.children("measure")) {
            for (const auto attributes : xml_measure.children("attributes")) {
                if (const auto node = attributes.child("divisions"))
                    divisions = node.text().as_int(kDivisions);
                if (const auto node = attributes.child("staves"))
                    ensure_staff(static_cast<std::size_t>(node.text().as_int(1)) - 1);
                for (const auto key : attributes.children("key")) {
                    const int fifths = key.child("fifths").text().as_int(0);
                    if (fifths < -7 || fifths > 7) continue; // not a v1 key
                    ensure_staff(static_cast<std::size_t>(attr_number_or(key, 1)) - 1);
                    part.staves[static_cast<std::size_t>(attr_number_or(key, 1)) - 1]
                        .key = KeySignature{fifths};
                }
                for (const auto time : attributes.children("time")) {
                    const int number = attr_number_or(time, 0);
                    const auto apply = [&](Staff& staff) {
                        if (time.attribute("senza-misura")) {
                            staff.time = TimeSignature::unmetered();
                        } else {
                            const int beats = time.child("beats").text().as_int(4);
                            const int beat_type =
                                time.child("beat-type").text().as_int(4);
                            if (beats > 0 && beat_type > 0)
                                staff.time = TimeSignature{beats, beat_type};
                        }
                    };
                    if (number > 0) {
                        ensure_staff(static_cast<std::size_t>(number) - 1);
                        apply(part.staves[static_cast<std::size_t>(number) - 1]);
                    } else {
                        for (auto& staff : part.staves) apply(staff);
                    }
                }
                for (const auto clef : attributes.children("clef")) {
                    const int number = attr_number_or(clef, 1);
                    ensure_staff(static_cast<std::size_t>(number) - 1);
                    Clef parsed(ClefKind::Treble);
                    if (clef_from_sign_line(clef.child_value("sign"),
                                            clef.child("line").text().as_int(0), parsed))
                        part.staves[static_cast<std::size_t>(number) - 1].clef = parsed;
                    // else: unknown clef sign, keep the staff's current clef
                }
                for (const auto transpose : attributes.children("transpose")) {
                    const int number = attr_number_or(transpose, 1);
                    ensure_staff(static_cast<std::size_t>(number) - 1);
                    part.staves[static_cast<std::size_t>(number) - 1].transpose_octaves =
                        transpose.child("octave-change").text().as_int(0);
                }
            }

            bool pending_extend_text = false;
            // The measure's musical time in divisions. <backup> rewinds it
            // (multi-voice / multi-staff measures), <forward> advances it;
            // events are collected with their time and interleaved by it, so
            // a voice that was backed up lands at its musical position.
            struct TimedObject {
                long long time = 0;
                long long seq = 0;
                std::size_t staff_index = 0;
                StaffObject object{};
            };
            std::vector<TimedObject> timed;
            long long now = 0;
            long long seq = 0;
            for (const auto xml_child : xml_measure.children()) {
                const std::string_view child_name = xml_child.name();
                if (child_name == "backup") {
                    const long long back =
                        xml_child.child("duration").text().as_llong(0);
                    now = std::max<long long>(0, now - back);
                    continue;
                }
                if (child_name == "forward") {
                    now += xml_child.child("duration").text().as_llong(0);
                    continue;
                }
                if (child_name != "note") continue; // attributes, barlines...
                const auto xml_note = xml_child;
                if (xml_note.child("grace")) continue; // v1: grace notes skipped
                const int staff_number = xml_note.child("staff")
                                             ? xml_note.child("staff").text().as_int(1)
                                             : 1;
                const std::size_t staff_index =
                    static_cast<std::size_t>(std::max(1, staff_number)) - 1;

                const int voice = xml_note.child("voice")
                                      ? xml_note.child("voice").text().as_int(1)
                                      : 1;
                const long long duration = xml_note.child("duration")
                                               ? xml_note.child("duration")
                                                     .text()
                                                     .as_llong()
                                               : 0;
                int dots = 0;
                for (const auto dot : xml_note.children("dot")) {
                    (void)dot;
                    ++dots;
                }
                NoteDuration duration_value{NoteValue::Quarter, dots};
                if (const auto type = xml_note.child("type")) {
                    try {
                        duration_value.value = note_value_from_type(type.child_value());
                    } catch (const std::runtime_error&) {
                        duration_value = nearest_duration(duration, divisions);
                    }
                } else {
                    duration_value = nearest_duration(duration, divisions);
                }

                if (xml_note.child("rest")) {
                    Rest rest;
                    rest.duration = duration_value;
                    (void)voice; // rests keep their position implicitly
                    timed.push_back({now, seq++, staff_index, std::move(rest)});
                    now += duration;
                    pending_extend_text = false;
                    continue;
                }

                NotationNote note;
                bool pitched_ok = false;
                if (const auto unpitched = xml_note.child("unpitched")) {
                    const std::string step = unpitched.child_value("display-step");
                    if (!step.empty()) {
                        note.spelled.step = step_from_letter(step[0]);
                        note.spelled.accidental = static_cast<Accidental>(0);
                        note.spelled.octave =
                            unpitched.child("display-octave").text().as_int(4);
                        pitched_ok = true;
                    }
                } else if (const auto pitch = xml_note.child("pitch")) {
                    const std::string step = pitch.child_value("step");
                    if (!step.empty()) {
                        note.spelled.step = step_from_letter(step[0]);
                        const int alter = pitch.child("alter")
                                              ? pitch.child("alter").text().as_int(0)
                                              : 0;
                        note.spelled.accidental =
                            static_cast<Accidental>(std::clamp(alter, -2, 2));
                        note.spelled.octave = pitch.child("octave").text().as_int(4);
                        pitched_ok = true;
                    }
                }
                if (!pitched_ok) continue; // neither pitch, unpitched nor rest: skip
                note.duration = duration_value;
                note.tie_start = has_tie(xml_note, "start");
                note.tie_end = has_tie(xml_note, "stop");
                note.voice = voice;
                note.duration.stemless =
                    xml_note.child("stem") &&
                    std::string(xml_note.child_value("stem")) == "none";

                if (const auto lyric = xml_note.child("lyric")) {
                    pending_extend_text = false;
                    const std::string text = lyric.child_value("text");
                    const bool extend = (bool)lyric.child("extend");
                    if (!text.empty()) {
                        note.lyric = text;
                        if (extend) pending_extend_text = true;
                    } else {
                        note.lyric = "-";
                    }
                } else if (pending_extend_text) {
                    note.lyric = "_";
                    pending_extend_text = false;
                }
                // <chord/> tones import as sequential notes in v1 (documented).

                timed.push_back({now, seq++, staff_index, std::move(note)});
                now += duration;
            }
            // The model's measure holds one timeline: interleave by musical
            // time, keeping document order for simultaneous events.
            std::stable_sort(timed.begin(), timed.end(),
                             [](const TimedObject& a, const TimedObject& b) {
                                 if (a.staff_index != b.staff_index)
                                     return a.staff_index < b.staff_index;
                                 return a.time < b.time;
                             });
            for (auto& t : timed) {
                ensure_staff(t.staff_index);
                Staff& staff = part.staves[t.staff_index];
                while (staff.measures.size() <= measure_index)
                    staff.measures.push_back(Measure{});
                staff.measures[measure_index].objects.emplace_back(
                    std::move(t.object));
            }
            ++measure_index;
        }

        // Every staff of the part ends up with the same measure count.
        std::size_t measure_count = 0;
        for (const auto& staff : part.staves)
            measure_count = std::max(measure_count, staff.measures.size());
        for (auto& staff : part.staves)
            while (staff.measures.size() < measure_count)
                staff.measures.push_back(Measure{});
    }

    return score;
}

} // namespace blokkily::notation
