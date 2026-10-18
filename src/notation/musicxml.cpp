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

#include "blokkily/notation/duration.hpp"
#include "blokkily/notation/lyrics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <optional>
#include <tuple>
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


// Staff numbers come from the document: a part has at most this many, so a
// hostile or broken file cannot make the importer build millions of staves.
constexpr int kMaxStaves = 16;

std::size_t staff_index(int number) {
    if (number < 1 || number > kMaxStaves)
        throw std::runtime_error("musicxml: staff number " + std::to_string(number) +
                                 " is outside 1-" + std::to_string(kMaxStaves));
    return static_cast<std::size_t>(number - 1);
}

// A decimal without trailing zeros: 1, -0.5, 1.25.
std::string decimal(double value) {
    std::ostringstream out;
    out.precision(6);
    out << value;
    return out.str();
}

// MusicXML's note dynamics are a percentage of forte, MIDI velocity 90.
double dynamics_of(float velocity) { return std::round(velocity * 127.0 / 90.0 * 10000.0) / 100.0; }
float velocity_of(double dynamics) {
    if (!std::isfinite(dynamics)) return 0.8F; // "nan", "inf": the default
    return static_cast<float>(std::clamp(dynamics / 100.0 * 90.0 / 127.0, 0.0, 1.0));
}

// ---------------------------------------------------------------------------
// Lyrics
// ---------------------------------------------------------------------------
// The model's syllables: "Al-" goes on into the next syllable of its word,
// "_" holds the previous syllable under this note (a lone "-", from older
// files, means the same). They are written as <syllabic> begin, middle, end
// or single, <extend/> on a held syllable, and <extend type="continue"/> or
// "stop" on the notes that hold it.

bool holds(const std::string& lyric) { return lyric == "_" || lyric == "-"; }

struct LyricOut {
    bool emit = false;
    std::string syllabic;
    std::string text;
    std::string extend; // "", "start", "continue" or "stop"
};

// One voice's notes in order; a null entry is a rest.
std::vector<LyricOut> plan_lyrics(const std::vector<const std::string*>& line) {
    std::vector<LyricOut> out(line.size());
    bool word_open = false;
    const auto held_next = [&](std::size_t i) {
        return i + 1 < line.size() && line[i + 1] != nullptr && holds(*line[i + 1]);
    };
    for (std::size_t i = 0; i < line.size(); ++i) {
        if (line[i] == nullptr || line[i]->empty()) continue;
        const std::string& lyric = *line[i];
        out[i].emit = true;
        if (holds(lyric)) {
            out[i].extend = held_next(i) ? "continue" : "stop";
            continue;
        }
        const bool hyphen = lyric.size() > 1 && lyric.back() == '-';
        out[i].text = hyphen ? lyric.substr(0, lyric.size() - 1) : lyric;
        out[i].syllabic = word_open ? (hyphen ? "middle" : "end") : (hyphen ? "begin" : "single");
        word_open = hyphen;
        if (held_next(i)) out[i].extend = "start";
    }
    return out;
}

void write_lyric(pugi::xml_node note, const LyricOut& lyric) {
    if (!lyric.emit) return;
    auto xml_lyric = note.append_child("lyric");
    xml_lyric.append_attribute("number") = "1";
    if (!lyric.text.empty()) {
        append_text(xml_lyric, "syllabic", lyric.syllabic);
        append_text(xml_lyric, "text", lyric.text);
    }
    if (!lyric.extend.empty()) {
        auto extend = xml_lyric.append_child("extend");
        extend.append_attribute("type") = lyric.extend.c_str();
    }
}

// ---------------------------------------------------------------------------
// Note writing, in the element order the MusicXML schema requires
// ---------------------------------------------------------------------------

void write_value(pugi::xml_node note, const NoteDuration& duration) {
    append_text(note, "type", note_type_name(duration.value));
    for (int d = 0; d < duration.dots; ++d) note.append_child("dot");
}

void write_tuplet(pugi::xml_node note, const NoteDuration& duration) {
    if (!duration.tuplet()) return;
    auto modification = note.append_child("time-modification");
    append_int(modification, "actual-notes", duration.tuplet_actual);
    append_int(modification, "normal-notes", duration.tuplet_normal);
}

void write_pitched_note(pugi::xml_node parent, const NotationNote& note, bool percussion,
                        bool multi_staff, int staff_number, const LyricOut& lyric) {
    auto xml_note = parent.append_child("note");
    xml_note.append_attribute("dynamics") = decimal(dynamics_of(note.velocity)).c_str();
    if (note.chord) xml_note.append_child("chord");
    if (percussion) {
        auto unpitched = xml_note.append_child("unpitched");
        append_text(unpitched, "display-step", std::string(1, step_letter(note.spelled.step)));
        append_int(unpitched, "display-octave", note.spelled.octave);
    } else {
        auto pitch = xml_note.append_child("pitch");
        append_text(pitch, "step", std::string(1, step_letter(note.spelled.step)));
        // A retune rides on the alteration, in fractions of a semitone.
        const double alter = accidental_value(note.spelled.accidental) + note.cents / 100.0;
        if (alter != 0.0) append_text(pitch, "alter", decimal(alter));
        append_int(pitch, "octave", note.spelled.octave);
    }
    append_int(xml_note, "duration", duration_divisions(note.duration));
    if (note.tie_end) xml_note.append_child("tie").append_attribute("type") = "stop";
    if (note.tie_start) xml_note.append_child("tie").append_attribute("type") = "start";
    append_int(xml_note, "voice", note.voice);
    write_value(xml_note, note.duration);
    write_tuplet(xml_note, note.duration);
    if (note.duration.stemless) append_text(xml_note, "stem", "none");
    if (multi_staff) append_int(xml_note, "staff", staff_number);
    if (note.tie_start || note.tie_end) {
        auto notations = xml_note.append_child("notations");
        if (note.tie_end) notations.append_child("tied").append_attribute("type") = "stop";
        if (note.tie_start) notations.append_child("tied").append_attribute("type") = "start";
    }
    write_lyric(xml_note, lyric);
}

void write_rest(pugi::xml_node parent, const Rest& rest, bool multi_staff, int staff_number) {
    auto xml_note = parent.append_child("note");
    xml_note.append_child("rest");
    append_int(xml_note, "duration", duration_divisions(rest.duration));
    append_int(xml_note, "voice", rest.voice);
    write_value(xml_note, rest.duration);
    write_tuplet(xml_note, rest.duration);
    if (multi_staff) append_int(xml_note, "staff", staff_number);
}

int voice_of(const StaffObject& object) {
    return std::visit([](const auto& o) { return o.voice; }, object);
}

// ---------------------------------------------------------------------------
// Reading helpers
// ---------------------------------------------------------------------------

bool has_tie(const pugi::xml_node& note, const char* kind) {
    for (auto tie : note.children("tie"))
        if (std::string(tie.attribute("type").value()) == kind) return true;
    if (const auto notations = note.child("notations"))
        for (auto tied : notations.children("tied"))
            if (std::string(tied.attribute("type").value()) == kind) return true;
    return false;
}

// A whole number from an element's text, or a runtime_error naming it.
long long whole_number(const pugi::xml_node& node, const char* what) {
    const std::string text = node.text().as_string();
    char* end = nullptr;
    const long long value = std::strtoll(text.c_str(), &end, 10);
    if (text.empty() || end == text.c_str() || *end != '\0')
        throw std::runtime_error(std::string("musicxml: <") + what + "> is not a whole number: '" +
                                 text + "'");
    return value;
}

// "3", or "3+2" for an additive meter.
int beats_of(const std::string& text) {
    int total = 0;
    std::size_t at = 0;
    while (at <= text.size()) {
        const auto plus = text.find('+', at);
        const std::string part = text.substr(at, plus == std::string::npos ? std::string::npos : plus - at);
        char* end = nullptr;
        const long value = std::strtol(part.c_str(), &end, 10);
        if (part.empty() || *end != '\0' || value < 1 || value > 64)
            throw std::runtime_error("musicxml: <beats> must be a count like 3 or 3+2, not '" + text + "'");
        total += static_cast<int>(value);
        if (total > 64)
            throw std::runtime_error("musicxml: <beats> adds up to more than 64: '" + text + "'");
        if (plus == std::string::npos) break;
        at = plus + 1;
    }
    return total;
}

TimeSignature time_of(const pugi::xml_node& time) {
    if (time.child("senza-misura") || time.attribute("senza-misura")) return TimeSignature::unmetered();
    const int beats = beats_of(time.child_value("beats"));
    const long long unit = whole_number(time.child("beat-type"), "beat-type");
    try {
        return TimeSignature{beats, static_cast<int>(std::clamp<long long>(unit, 0, 64))};
    } catch (const std::invalid_argument& error) {
        throw std::runtime_error(std::string("musicxml: unsupported time signature: ") + error.what());
    }
}

NoteDuration written_value(const pugi::xml_node& note) {
    NoteDuration value{};
    value.value = note_value_from_type(note.child_value("type"));
    for (const auto dot : note.children("dot")) {
        (void)dot;
        ++value.dots;
    }
    if (const auto modification = note.child("time-modification")) {
        const int actual = modification.child("actual-notes").text().as_int(1);
        const int normal = modification.child("normal-notes").text().as_int(1);
        if (actual >= 1 && normal >= 1 && actual <= 64 && normal <= 64) {
            value.tuplet_actual = actual;
            value.tuplet_normal = normal;
        }
    }
    return value;
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
    if (!score.title.empty()) append_text(root.append_child("work"), "work-title", score.title);
    if (!score.composer.empty()) {
        auto creator = root.append_child("identification").append_child("creator");
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
        std::size_t measure_count = 1; // keep the part valid
        for (const auto& staff : part.staves) measure_count = std::max(measure_count, staff.measures.size());
        const auto measure_at = [&](const Staff& staff, std::size_t m) -> const Measure* {
            return m < staff.measures.size() ? &staff.measures[m] : nullptr;
        };

        // Lyrics are planned along each voice of each staff, across barlines.
        std::map<std::tuple<std::size_t, std::size_t, std::size_t>, LyricOut> lyrics;
        for (std::size_t s = 0; s < part.staves.size(); ++s) {
            std::map<int, std::vector<std::pair<std::pair<std::size_t, std::size_t>, const std::string*>>> lines;
            const auto& measures = part.staves[s].measures;
            for (std::size_t m = 0; m < measures.size(); ++m)
                for (std::size_t i = 0; i < measures[m].objects.size(); ++i) {
                    const auto& object = measures[m].objects[i];
                    if (const auto* note = std::get_if<NotationNote>(&object)) {
                        if (!note->chord) lines[note->voice].push_back({{m, i}, &note->lyric});
                    } else {
                        lines[voice_of(object)].push_back({{m, i}, nullptr});
                    }
                }
            for (const auto& [voice, line] : lines) {
                std::vector<const std::string*> texts;
                for (const auto& entry : line) texts.push_back(entry.second);
                const auto planned = plan_lyrics(texts);
                for (std::size_t k = 0; k < line.size(); ++k)
                    lyrics[{s, line[k].first.first, line[k].first.second}] = planned[k];
            }
        }

        bool pickup_first = false;
        for (const auto& staff : part.staves)
            if (const auto* first = measure_at(staff, 0); first != nullptr && first->pickup) pickup_first = true;

        for (std::size_t m = 0; m < measure_count; ++m) {
            auto xml_measure = xml_part.append_child("measure");
            xml_measure.append_attribute("number") =
                std::to_string(pickup_first ? m : m + 1).c_str();
            bool pickup = false;
            for (const auto& staff : part.staves)
                if (const auto* measure = measure_at(staff, m); measure != nullptr && measure->pickup) pickup = true;
            if (pickup) xml_measure.append_attribute("implicit") = "yes";

            // What each staff declares here: everything in the first measure,
            // and later only what a measure changes.
            struct Declared {
                std::optional<KeySignature> key;
                std::optional<TimeSignature> time;
                std::optional<Clef> clef;
                std::optional<Transposition> transpose;
            };
            std::vector<Declared> declared(part.staves.size());
            bool any = m == 0;
            for (std::size_t s = 0; s < part.staves.size(); ++s) {
                const Staff& staff = part.staves[s];
                const Measure* measure = measure_at(staff, m);
                if (m == 0) {
                    declared[s] = {staff.key, staff.time, staff.clef, std::nullopt};
                    if (staff.transposition() != 0)
                        declared[s].transpose = Transposition{staff.transpose_semitones, staff.transpose_octaves};
                }
                if (measure != nullptr) {
                    if (measure->key) declared[s].key = measure->key;
                    if (measure->time) declared[s].time = measure->time;
                    if (measure->clef) declared[s].clef = measure->clef;
                    if (measure->transpose) declared[s].transpose = measure->transpose;
                }
                any = any || declared[s].key || declared[s].time || declared[s].clef || declared[s].transpose;
            }
            if (any) {
                auto attributes = xml_measure.append_child("attributes");
                if (m == 0) append_int(attributes, "divisions", kDivisions);
                for (std::size_t s = 0; s < part.staves.size(); ++s) {
                    if (!declared[s].key) continue;
                    auto key = attributes.append_child("key");
                    if (multi_staff) key.append_attribute("number") = static_cast<int>(s + 1);
                    append_int(key, "fifths", declared[s].key->fifths());
                }
                for (std::size_t s = 0; s < part.staves.size(); ++s) {
                    if (!declared[s].time) continue;
                    auto time = attributes.append_child("time");
                    if (multi_staff) time.append_attribute("number") = static_cast<int>(s + 1);
                    if (!declared[s].time->metered()) {
                        time.append_child("senza-misura");
                    } else {
                        append_int(time, "beats", declared[s].time->beats());
                        append_int(time, "beat-type", declared[s].time->beat_unit());
                    }
                }
                if (m == 0 && multi_staff)
                    append_int(attributes, "staves", static_cast<long long>(part.staves.size()));
                for (std::size_t s = 0; s < part.staves.size(); ++s) {
                    if (!declared[s].clef) continue;
                    const auto [sign, line] = clef_sign_line(*declared[s].clef);
                    auto clef = attributes.append_child("clef");
                    if (multi_staff) clef.append_attribute("number") = static_cast<int>(s + 1);
                    append_text(clef, "sign", sign);
                    if (line > 0) append_int(clef, "line", line);
                }
                // A change back to concert pitch is written too, as zero.
                for (std::size_t s = 0; s < part.staves.size(); ++s) {
                    if (!declared[s].transpose) continue;
                    auto transpose = attributes.append_child("transpose");
                    if (multi_staff) transpose.append_attribute("number") = static_cast<int>(s + 1);
                    append_int(transpose, "chromatic", declared[s].transpose->semitones);
                    if (declared[s].transpose->octaves != 0)
                        append_int(transpose, "octave-change", declared[s].transpose->octaves);
                }
            }

            // The tempo, at the start of the first part.
            if (m == 0 && p == 0 && score.tempo_bpm) {
                auto direction = xml_measure.append_child("direction");
                direction.append_attribute("placement") = "above";
                auto metronome = direction.append_child("direction-type").append_child("metronome");
                append_text(metronome, "beat-unit", "quarter");
                append_text(metronome, "per-minute", decimal(*score.tempo_bpm));
                direction.append_child("sound").append_attribute("tempo") = decimal(*score.tempo_bpm).c_str();
            }

            // Each voice of each staff from the measure's start, rewound to
            // it with <backup> before the next.
            long long previous = -1;
            for (std::size_t s = 0; s < part.staves.size(); ++s) {
                const Measure* measure = measure_at(part.staves[s], m);
                if (measure == nullptr) continue;
                const int staff_number = static_cast<int>(s + 1);
                const bool percussion = !part.staves[s].clef.pitched();
                std::vector<int> voices;
                for (const auto& object : measure->objects)
                    if (std::find(voices.begin(), voices.end(), voice_of(object)) == voices.end())
                        voices.push_back(voice_of(object));
                for (const int voice : voices) {
                    if (previous > 0) append_int(xml_measure.append_child("backup"), "duration", previous);
                    previous = 0;
                    for (std::size_t i = 0; i < measure->objects.size(); ++i) {
                        const auto& object = measure->objects[i];
                        if (voice_of(object) != voice) continue;
                        if (const auto* rest = std::get_if<Rest>(&object)) {
                            write_rest(xml_measure, *rest, multi_staff, staff_number);
                            previous += duration_divisions(rest->duration);
                            continue;
                        }
                        const auto& note = std::get<NotationNote>(object);
                        const auto planned = lyrics.find({s, m, i});
                        write_pitched_note(xml_measure, note, percussion, multi_staff, staff_number,
                                           planned != lyrics.end() ? planned->second : LyricOut{});
                        if (!note.chord) previous += duration_divisions(note.duration);
                    }
                }
            }

            bool metered = false;
            for (const auto& staff : part.staves) metered = metered || staff.time.metered();
            if (m + 1 == measure_count && metered) {
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
    if (!result) throw std::runtime_error(std::string("musicxml: parse error: ") + result.description());
    const auto root = doc.child("score-partwise");
    if (!root)
        throw std::runtime_error(
            "musicxml: expected a <score-partwise> document (timewise is not supported)");

    Score score;
    score.title = root.child("work").child_value("work-title");
    if (score.title.empty()) score.title = root.child_value("movement-title");
    if (const auto identification = root.child("identification"))
        for (const auto creator : identification.children("creator"))
            if (std::string(creator.attribute("type").value()) == "composer") {
                score.composer = creator.child_value();
                break;
            }

    // Part order follows the part list; parts missing from it are appended.
    std::map<std::string, std::size_t> id_to_index;
    if (const auto part_list = root.child("part-list"))
        for (const auto score_part : part_list.children("score-part")) {
            id_to_index[score_part.attribute("id").value()] = score.parts.size();
            Part part;
            part.name = score_part.child_value("part-name");
            score.parts.push_back(std::move(part));
        }

    for (const auto xml_part : root.children("part")) {
        const std::string id = xml_part.attribute("id").value();
        auto found = id_to_index.find(id);
        if (found == id_to_index.end()) {
            found = id_to_index.emplace(id, score.parts.size()).first;
            score.parts.push_back(Part{});
        }
        Part& part = score.parts[found->second];
        const auto ensure_staff = [&](std::size_t index) {
            while (part.staves.size() <= index) part.staves.push_back(default_staff());
        };
        ensure_staff(0);

        long long divisions = kDivisions;
        // Per staff: what its <transpose> and its clef's octave change say,
        // which the model adds into one transposition.
        struct Sounding {
            int chromatic = 0;
            int octaves = 0;
            int clef_octaves = 0;
            [[nodiscard]] Transposition total() const { return {chromatic, octaves + clef_octaves}; }
        };
        std::map<std::size_t, Sounding> sounding;
        // Per staff and voice: whether a held syllable is still running.
        std::map<std::pair<std::size_t, int>, bool> extending;
        std::size_t m = 0;
        for (const auto xml_measure : xml_part.children("measure")) {
            const auto measure_of = [&](std::size_t s) -> Measure& {
                ensure_staff(s);
                auto& measures = part.staves[s].measures;
                while (measures.size() <= m) measures.push_back(Measure{});
                return measures[m];
            };
            // In the first measure a declaration is the staff's own; later it
            // is a change written at that measure.
            const auto declare = [&](std::size_t s, const auto& apply_staff, const auto& apply_measure) {
                ensure_staff(s);
                if (m == 0) apply_staff(part.staves[s]);
                else apply_measure(measure_of(s));
            };
            const auto staves_named = [&](const pugi::xml_node& node) {
                std::vector<std::size_t> out;
                if (const auto number = node.attribute("number")) out.push_back(staff_index(number.as_int(0)));
                else
                    for (std::size_t s = 0; s < part.staves.size(); ++s) out.push_back(s);
                return out;
            };

            // The pieces the last note that is not a chord tone was written
            // as, by where they sit in its measure: a chord tone's pieces go
            // beside them, each starting with the lead's piece.
            struct Lead {
                std::size_t staff = 0;
                std::vector<std::size_t> at{};
                std::vector<NoteDuration> values{};
            };
            std::optional<Lead> lead;

            // Time in this measure, and where each voice of each staff has
            // got to, in kDivisions per quarter.
            long long now = 0;
            std::map<std::pair<std::size_t, int>, long long> cursor;
            std::map<std::pair<std::size_t, int>, long long> onset;
            // Any one duration is at most 1024 whole notes: a larger one is
            // not music, and would be written out as millions of tied rests.
            const auto to_units = [&](long long amount) {
                const double units = static_cast<double>(amount) * kDivisions / static_cast<double>(divisions);
                if (!(std::abs(units) <= 1024.0 * 4 * kDivisions))
                    throw std::runtime_error("musicxml: a duration is longer than 1024 whole notes");
                return static_cast<long long>(std::llround(units));
            };

            for (const auto child : xml_measure.children()) {
                const std::string_view name = child.name();
                if (name == "attributes") {
                    if (const auto node = child.child("divisions")) {
                        divisions = whole_number(node, "divisions");
                        if (divisions < 1 || divisions > 1'000'000)
                            throw std::runtime_error("musicxml: <divisions> must be positive");
                    }
                    if (const auto node = child.child("staves"))
                        ensure_staff(staff_index(static_cast<int>(whole_number(node, "staves"))));
                    for (const auto key : child.children("key")) {
                        const int fifths = key.child("fifths").text().as_int(0);
                        if (fifths < -7 || fifths > 7) continue; // not a key the model has
                        for (const auto s : staves_named(key))
                            declare(s, [&](Staff& st) { st.key = KeySignature{fifths}; },
                                    [&](Measure& me) { me.key = KeySignature{fifths}; });
                    }
                    for (const auto time : child.children("time")) {
                        const TimeSignature parsed = time_of(time);
                        for (const auto s : staves_named(time))
                            declare(s, [&](Staff& st) { st.time = parsed; },
                                    [&](Measure& me) { me.time = parsed; });
                    }
                    for (const auto clef : child.children("clef")) {
                        const std::size_t s = staff_index(attr_number_or(clef, 1));
                        Clef parsed(ClefKind::Treble);
                        if (!clef_from_sign_line(clef.child_value("sign"), clef.child("line").text().as_int(0), parsed))
                            continue; // a clef the model does not know keeps the current one
                        const int octave = std::clamp(clef.child("clef-octave-change").text().as_int(0), -2, 2);
                        const Transposition before = sounding[s].total();
                        sounding[s].clef_octaves = octave;
                        declare(s,
                                [&](Staff& st) {
                                    st.clef = parsed;
                                    st.transpose_octaves += octave;
                                },
                                [&](Measure& me) {
                                    me.clef = parsed;
                                    if (sounding[s].total() != before) me.transpose = sounding[s].total();
                                });
                    }
                    for (const auto transpose : child.children("transpose")) {
                        const int chromatic = std::clamp(transpose.child("chromatic").text().as_int(0), -24, 24);
                        const int octaves = std::clamp(transpose.child("octave-change").text().as_int(0), -4, 4);
                        for (const auto s : staves_named(transpose)) {
                            sounding[s].chromatic = chromatic;
                            sounding[s].octaves = octaves;
                            declare(s,
                                    [&](Staff& st) {
                                        st.transpose_semitones = chromatic;
                                        st.transpose_octaves += octaves;
                                    },
                                    [&](Measure& me) { me.transpose = sounding[s].total(); });
                        }
                    }
                    continue;
                }
                if (name == "backup") {
                    now = std::max<long long>(0, now - to_units(whole_number(child.child("duration"), "duration")));
                    continue;
                }
                if (name == "forward") {
                    now += to_units(whole_number(child.child("duration"), "duration"));
                    continue;
                }
                if (!score.tempo_bpm) {
                    // The first tempo the score sets, wherever it says it.
                    const auto sound = name == "sound" ? child : child.child("sound");
                    const double bpm = sound.attribute("tempo").as_double(0.0);
                    if (std::isfinite(bpm) && bpm > 0.0) score.tempo_bpm = bpm;
                }
                if (name != "note") continue; // barlines, directions, harmony...
                const auto xml_note = child;
                if (xml_note.child("grace")) continue; // grace notes take no time; not modelled

                const std::size_t s =
                    xml_note.child("staff") ? staff_index(static_cast<int>(whole_number(xml_note.child("staff"), "staff"))) : 0;
                const int voice = std::clamp(xml_note.child("voice").text().as_int(1), 1, 64);
                const auto where = std::make_pair(s, voice);
                Measure& measure = measure_of(s);
                const bool chord = static_cast<bool>(xml_note.child("chord"));
                if (!chord) lead.reset();
                const long long length =
                    xml_note.child("duration") ? to_units(whole_number(xml_note.child("duration"), "duration")) : -1;
                if (length < -1) throw std::runtime_error("musicxml: a <duration> is negative");

                // The written value, or what the duration says when there is
                // no <type> or the two disagree (a sloppy writer's tuplet).
                std::vector<NoteDuration> values;
                bool typed = false;
                if (xml_note.child("type")) {
                    try {
                        values.push_back(written_value(xml_note));
                        typed = true;
                    } catch (const std::runtime_error&) {
                    }
                }
                if (length > 0 && (!typed || ticks(values.front(), kDivisions) != length) && !chord)
                    values = notate_ticks(length, kDivisions);
                if (values.empty()) {
                    if (length <= 0) continue; // neither a value nor a duration: nothing to place
                    values = notate_ticks(length, kDivisions);
                }
                long long span = 0;
                for (const auto& value : values) span += ticks(value, kDivisions);
                if (xml_note.child("cue")) {
                    if (!chord) now += span;
                    continue;
                }

                // Where this voice is in the model, padded with a rest up to
                // the note's time when the file moved on without it.
                long long& at = cursor[where];
                if (!chord) {
                    if (now > at)
                        for (const auto& gap : notate_ticks(now - at, kDivisions))
                            measure.objects.emplace_back(Rest{gap, voice});
                    onset[where] = std::max(now, at);
                    at = onset[where] + span;
                    now = at;
                }

                if (xml_note.child("rest")) {
                    for (const auto& value : values) measure.objects.emplace_back(Rest{value, voice});
                    extending[where] = false;
                    continue;
                }

                NotationNote note;
                if (const auto unpitched = xml_note.child("unpitched")) {
                    const std::string step = unpitched.child_value("display-step");
                    if (step.empty()) continue;
                    note.spelled.step = step_from_letter(step[0]);
                    note.spelled.octave = std::clamp(unpitched.child("display-octave").text().as_int(4), -1, 10);
                } else if (const auto pitch = xml_note.child("pitch")) {
                    const std::string step = pitch.child_value("step");
                    if (step.empty()) continue;
                    note.spelled.step = step_from_letter(step[0]);
                    // A fractional alteration is a retune: the nearest
                    // accidental is spelled, the rest sounds as cents.
                    const double alter = pitch.child("alter").text().as_double(0.0);
                    if (!std::isfinite(alter) || alter < -3.0 || alter > 3.0)
                        throw std::runtime_error("musicxml: <alter> is out of range");
                    // Halves round toward the written note, so a quarter tone
                    // either way keeps its letter and accidental.
                    const double nearest = std::ceil(std::abs(alter) - 0.5);
                    const int accidental =
                        std::clamp(static_cast<int>(alter < 0 ? -nearest : nearest), -2, 2);
                    note.spelled.accidental = static_cast<Accidental>(accidental);
                    note.cents = std::round((alter - accidental) * 100.0 * 1000.0) / 1000.0;
                    note.spelled.octave = std::clamp(pitch.child("octave").text().as_int(4), -1, 10);
                } else {
                    continue;
                }
                note.voice = voice;
                note.chord = chord;
                if (const auto dynamics = xml_note.attribute("dynamics"))
                    note.velocity = velocity_of(dynamics.as_double(dynamics_of(0.8F)));
                const bool stemless = std::string(xml_note.child_value("stem")) == "none";

                // The syllable the first piece carries.
                std::string lyric;
                if (const auto xml_lyric = xml_note.child("lyric")) {
                    const std::string text = xml_lyric.child_value("text");
                    const std::string syllabic = xml_lyric.child_value("syllabic");
                    const auto extend = xml_lyric.child("extend");
                    if (!text.empty()) {
                        const bool joins = syllabic == "begin" || syllabic == "middle";
                        lyric = fit_lyric(joins ? text + "-" : text);
                        extending[where] = static_cast<bool>(extend);
                    } else if (extend) {
                        lyric = "_";
                        extending[where] = std::string(extend.attribute("type").value()) != "stop";
                    }
                } else if (!chord && extending[where]) {
                    lyric = "_";
                }

                // A chord tone split into more values than its lead takes the
                // lead's values: a piece with nothing to sound beside would
                // be read as the voice's next note.
                const bool beside = chord && lead && lead->staff == s && !lead->at.empty();
                if (beside && values.size() > lead->at.size()) values = lead->values;
                if (!chord) lead = Lead{s, {}, values};
                for (std::size_t k = 0; k < values.size(); ++k) {
                    NotationNote piece = note;
                    piece.duration = values[k];
                    piece.duration.stemless = stemless;
                    piece.tie_end = k > 0 || has_tie(xml_note, "stop");
                    piece.tie_start = k + 1 < values.size() || has_tie(xml_note, "start");
                    if (k == 0) piece.lyric = lyric;
                    if (beside) {
                        // After the lead's k-th piece and the tones already
                        // beside it; the pieces after it move up one.
                        const std::size_t where = lead->at[k] + 1;
                        measure.objects.insert(measure.objects.begin() + static_cast<std::ptrdiff_t>(where),
                                               std::move(piece));
                        lead->at[k] = where;
                        for (std::size_t j = k + 1; j < lead->at.size(); ++j) ++lead->at[j];
                        continue;
                    }
                    if (k > 0) piece.chord = false;
                    if (!chord) lead->at.push_back(measure.objects.size());
                    measure.objects.emplace_back(std::move(piece));
                }
            }
            if (std::string(xml_measure.attribute("implicit").value()) == "yes")
                for (std::size_t s = 0; s < part.staves.size(); ++s) measure_of(s).pickup = true;
            for (std::size_t s = 0; s < part.staves.size(); ++s) (void)measure_of(s);
            ++m;
        }

        // Every staff of the part ends up with the same measure count.
        std::size_t measure_count = 0;
        for (const auto& staff : part.staves) measure_count = std::max(measure_count, staff.measures.size());
        for (auto& staff : part.staves)
            while (staff.measures.size() < measure_count) staff.measures.push_back(Measure{});
    }
    return score;
}

} // namespace blokkily::notation
