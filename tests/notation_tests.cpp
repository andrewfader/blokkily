// Staff notation model tests (task A of the staff-notation project).
//
// features/notation.feature: each case below names the scenario it proves.
// The model scenarios are proved here; the score-view scenarios are task C's,
// proved by the screenshot gate.

#include "blokkily/notation/clef.hpp"
#include "blokkily/notation/duration.hpp"
#include "blokkily/notation/layout.hpp"
#include "blokkily/notation/lyrics.hpp"
#include "blokkily/notation/musicxml.hpp"
#include "blokkily/notation/song_score.hpp"
#include "blokkily/notation/note.hpp"
#include "blokkily/notation/pattern_bridge.hpp"
#include "blokkily/notation/pitch.hpp"
#include "blokkily/notation/signature.hpp"
#include "blokkily/notation/spelling.hpp"
#include "blokkily/notation/staff.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using namespace blokkily;
using namespace blokkily::notation;

namespace {

int failures = 0;

void require(bool value, const std::string& message) {
    if (!value) {
        ++failures;
        std::cerr << "FAIL: " << message << "\n";
    }
}

std::string spelled_name(const SpelledPitch& p) {
    std::string s(step_name(p.step));
    s += accidental_name(p.accidental);
    s += std::to_string(p.octave);
    return s;
}

// A pattern note's sounding identity: start tick, MIDI key, duration ticks.
struct Sounding {
    Tick start;
    int key;
    Tick duration;
    bool operator==(const Sounding& o) const {
        return start == o.start && key == o.key && duration == o.duration;
    }
    bool operator<(const Sounding& o) const {
        if (start != o.start) return start < o.start;
        return key < o.key;
    }
};

std::vector<Sounding> soundings_of(const Pattern& pattern) {
    std::vector<Sounding> out;
    for (const Trigger& t : pattern.events()) {
        if (const auto* n = std::get_if<Note>(&t.musical_data))
            out.push_back({t.start, n->key, t.duration});
        else if (const auto* c = std::get_if<Chord>(&t.musical_data))
            for (std::size_t i = 0; i < c->intervals.size(); ++i)
                out.push_back({t.start, c->root + c->intervals[i],
                               voice_duration(*c, i, t.duration)});
    }
    std::sort(out.begin(), out.end());
    return out;
}

void test_pitch() {
    // Scenario: Pitches convert between spelling and MIDI both ways
    require(to_midi(SpelledPitch{Step::C, Accidental::Natural, 4}) == 60,
            "C4 is MIDI 60");
    require(to_midi(SpelledPitch{Step::B, Accidental::Sharp, 3}) == 60,
            "B#3 is MIDI 60");
    require(to_midi(SpelledPitch{Step::C, Accidental::Flat, 4}) == 59,
            "Cb4 is MIDI 59");
    require(to_midi(SpelledPitch{Step::A, Accidental::Natural, 4}) == 69,
            "A4 is MIDI 69");
    for (int k = 0; k < 128; ++k)
        require(to_midi(from_midi(k)) == k, "from_midi round-trips MIDI " + std::to_string(k));
    require(spelled_name(from_midi(61)) == "C#4", "from_midi spells 61 sharp-side");
    bool threw = false;
    try { (void)from_midi(128); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "from_midi rejects key 128");
    require(diatonic_index(SpelledPitch{Step::C, Accidental::Natural, 4}) == 28,
            "C4 is diatonic index 28");
}

void test_clefs() {
    // Scenario: Every clef names itself and places the staff's bottom line
    const std::vector<std::pair<ClefKind, std::string>> names = {
        {ClefKind::FrenchViolin, "French violin"}, {ClefKind::Treble, "Treble"},
        {ClefKind::Soprano, "Soprano"}, {ClefKind::MezzoSoprano, "Mezzo-soprano"},
        {ClefKind::Alto, "Alto"}, {ClefKind::Tenor, "Tenor"},
        {ClefKind::BaritoneC, "Baritone (C)"}, {ClefKind::BaritoneF, "Baritone (F)"},
        {ClefKind::Bass, "Bass"}, {ClefKind::Subbass, "Subbass"},
        {ClefKind::Percussion, "Percussion"},
    };
    require(names.size() == 11, "eleven clefs");
    for (const auto& [kind, name] : names)
        require(Clef(kind).name() == name, "clef name: " + name);
    // Bottom lines, as absolute diatonic indices (C4 == 28).
    require(Clef(ClefKind::Treble).bottom_line_diatonic() == 30, "treble bottom is E4");
    require(Clef(ClefKind::FrenchViolin).bottom_line_diatonic() == 32,
            "french violin bottom is G4");
    require(Clef(ClefKind::Soprano).bottom_line_diatonic() == 28, "soprano bottom is C4");
    require(Clef(ClefKind::MezzoSoprano).bottom_line_diatonic() == 26,
            "mezzo bottom is A3");
    require(Clef(ClefKind::Alto).bottom_line_diatonic() == 24, "alto bottom is F3");
    require(Clef(ClefKind::Tenor).bottom_line_diatonic() == 22, "tenor bottom is D3");
    require(Clef(ClefKind::BaritoneC).bottom_line_diatonic() == 20,
            "baritone-C bottom is B2");
    require(Clef(ClefKind::BaritoneF).bottom_line_diatonic() == 20,
            "baritone-F bottom is B2, agreeing with baritone-C");
    require(Clef(ClefKind::Bass).bottom_line_diatonic() == 18, "bass bottom is G2");
    require(Clef(ClefKind::Subbass).bottom_line_diatonic() == 18 - 2,
            "subbass bottom is E2");
    require(!Clef(ClefKind::Percussion).pitched(), "percussion is unpitched");
    require(Clef(ClefKind::Treble).pitched(), "treble is pitched");
}

void test_key_signature() {
    // Scenario: Key signatures alter the steps they claim, no others
    bool threw = false;
    try { KeySignature k(8); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "8 sharps throws");
    threw = false;
    try { KeySignature k(-8); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "-8 flats throws");
    const KeySignature d_major(2);
    require(d_major.accidental_for(Step::F) == Accidental::Sharp, "D major: F#");
    require(d_major.accidental_for(Step::C) == Accidental::Sharp, "D major: C#");
    require(d_major.accidental_for(Step::G) == Accidental::Natural, "D major: G natural");
    const KeySignature bb_major(-2);
    require(bb_major.accidental_for(Step::B) == Accidental::Flat, "Bb major: Bb");
    require(bb_major.accidental_for(Step::E) == Accidental::Flat, "Bb major: Eb");
    require(bb_major.accidental_for(Step::A) == Accidental::Natural, "Bb major: A natural");
    require(spelled_name(d_major.major_tonic()) == "D4", "2 sharps is D major");
    require(spelled_name(KeySignature(-3).major_tonic()) == "Eb4", "-3 flats is Eb major");
    require(spelled_name(KeySignature(6).major_tonic()) == "F#4", "6 sharps is F# major");
    require(spelled_name(KeySignature(0).minor_tonic()) == "A4", "C major's relative is A minor");
    // Regression: the flat-side tonic tables were shifted by one (2026-10-16).
    // Every flat key's major and minor tonic, -7..0.
    require(spelled_name(KeySignature(-7).major_tonic()) == "Cb4", "-7 flats is Cb major");
    require(spelled_name(KeySignature(-6).major_tonic()) == "Gb4", "-6 flats is Gb major");
    require(spelled_name(KeySignature(-5).major_tonic()) == "Db4", "-5 flats is Db major");
    require(spelled_name(KeySignature(-4).major_tonic()) == "Ab4", "-4 flats is Ab major");
    require(spelled_name(KeySignature(-2).major_tonic()) == "Bb4", "-2 flats is Bb major");
    require(spelled_name(KeySignature(-1).major_tonic()) == "F4", "-1 flat is F major");
    require(spelled_name(KeySignature(-7).minor_tonic()) == "Ab4", "-7 flats is Ab minor");
    require(spelled_name(KeySignature(-6).minor_tonic()) == "Eb4", "-6 flats is Eb minor");
    require(spelled_name(KeySignature(-5).minor_tonic()) == "Bb4", "-5 flats is Bb minor");
    require(spelled_name(KeySignature(-4).minor_tonic()) == "F4", "-4 flats is F minor");
    require(spelled_name(KeySignature(-3).minor_tonic()) == "C4", "-3 flats is C minor");
    require(spelled_name(KeySignature(-2).minor_tonic()) == "G4", "-2 flats is G minor");
    require(spelled_name(KeySignature(-1).minor_tonic()) == "D4", "-1 flat is D minor");
    require(d_major.label() == "D major", "key label");
}

void test_spelling() {
    // Scenario: Enharmonic spelling follows the key signature
    const KeySignature d_major(2);
    require(spelled_name(spell(66, d_major)) == "F#4", "F# in D major, not Gb");
    require(spelled_name(spell(61, d_major)) == "C#4", "C# in D major");
    const KeySignature f_major(-1);
    require(spelled_name(spell(70, f_major)) == "Bb4", "Bb in F major, not A#");
    require(spelled_name(spell(61, f_major)) == "Db4", "Db in F major");
    const KeySignature c_major(0);
    require(spelled_name(spell(61, c_major)) == "C#4", "C major breaks ties sharp-side");
    require(spelled_name(spell(60, c_major)) == "C4", "C4 is C4");
    // The key's own altered spellings win, even the exotic ones.
    require(spelled_name(spell(60, KeySignature(7))) == "B#3", "C# major spells 60 as B#");
    require(spelled_name(spell(60, KeySignature(-7))) == "C4", "Cb major spells 60 as C natural");
    require(spelled_name(spell(61, KeySignature(-7))) == "Db4", "Cb major spells 61 as Db");
    bool threw = false;
    try { (void)spell(-1, c_major); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "spell rejects key -1");
}

void test_durations() {
    // Scenario: Note values convert to ticks and back; odd lengths tie
    constexpr Tick tpb = 480;
    require(ticks(NoteDuration{NoteValue::Quarter}, tpb) == 480, "quarter is 480");
    require(ticks(NoteDuration{NoteValue::Half}, tpb) == 960, "half is 960");
    require(ticks(NoteDuration{NoteValue::Whole}, tpb) == 1920, "whole is 1920");
    require(ticks(NoteDuration{NoteValue::Breve}, tpb) == 3840, "breve is 3840");
    require(ticks(NoteDuration{NoteValue::Eighth}, tpb) == 240, "eighth is 240");
    require(ticks(NoteDuration{NoteValue::HundredTwentyEighth}, tpb) == 15, "128th is 15");
    require(ticks(NoteDuration{NoteValue::Half, 1}, tpb) == 1440, "dotted half is 1440");
    require(ticks(NoteDuration{NoteValue::Quarter, 2}, tpb) == 840,
            "double-dotted quarter is 840");
    const auto dotted_quarter = single_from_ticks(720, tpb);
    require(dotted_quarter.has_value() && dotted_quarter->value == NoteValue::Quarter &&
                dotted_quarter->dots == 1,
            "720 ticks is a dotted quarter");
    require(!single_from_ticks(500, tpb).has_value(), "500 ticks is not exact");
    const auto chain = from_ticks(720, tpb);
    require(chain.size() == 1 && chain[0].dots == 1, "720 ties to one dotted half");
    const auto tied = from_ticks(2880, tpb);
    require(tied.size() == 2 && tied[0].value == NoteValue::Whole &&
                tied[1].value == NoteValue::Half,
            "2880 ties whole + half");
    // 500 rounds to the 15-tick grid (495) and ties quarter + 128th.
    const auto odd = from_ticks(500, tpb);
    require(ticks(odd, tpb) == 495, "500 rounds to 495 on the 128th grid");
    require(odd.size() == 2 && odd[0].value == NoteValue::Quarter &&
                odd[1].value == NoteValue::HundredTwentyEighth,
            "495 ties quarter + 128th");
    bool threw = false;
    try { (void)from_ticks(0, tpb); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "from_ticks rejects 0");
}

void test_layout() {
    // Scenario: A spelled pitch lands on the right line or space of its clef
    const Clef treble(ClefKind::Treble);
    require(staff_position(SpelledPitch{Step::E, Accidental::Natural, 4}, treble) == 0,
            "E4 is treble's bottom line");
    require(staff_position(SpelledPitch{Step::C, Accidental::Natural, 4}, treble) == -2,
            "middle C hangs two steps below treble's bottom line");
    require(staff_position(SpelledPitch{Step::G, Accidental::Natural, 4}, treble) == 2,
            "G4 is treble's second line");
    const Clef bass(ClefKind::Bass);
    require(staff_position(SpelledPitch{Step::G, Accidental::Natural, 2}, bass) == 0,
            "G2 is bass's bottom line");
    // Accidentals show exactly when the key does not already say them.
    const KeySignature d_major(2);
    require(!needs_accidental(SpelledPitch{Step::F, Accidental::Sharp, 4}, d_major),
            "F# needs no accidental in D major");
    require(needs_accidental(SpelledPitch{Step::F, Accidental::Sharp, 4}, KeySignature(0)),
            "F# needs an accidental in C major");
    require(needs_accidental(SpelledPitch{Step::F, Accidental::Natural, 4}, d_major),
            "F natural needs a natural in D major");
    // Ledger lines.
    require(ledger_lines(-2) == 1, "middle C below treble takes one ledger line");
    require(ledger_lines(-1) == 0, "the space below needs none");
    require(ledger_lines(8) == 0, "top line needs none");
    require(ledger_lines(9) == 0, "the space above needs none");
    require(ledger_lines(10) == 1, "first ledger line above");
    require(ledger_lines(12) == 2, "C6 above treble takes two");
    // Time signatures, including chant.
    const TimeSignature common(4, 4);
    require(common.metered() && common.label() == "4/4", "4/4 is metered");
    const auto chant = TimeSignature::unmetered();
    require(!chant.metered() && chant.label() == "unmetered", "chant is unmetered");
    bool threw = false;
    try { TimeSignature t(0, 4); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "0 beats throws");
    threw = false;
    try { TimeSignature t(4, 3); } catch (const std::invalid_argument&) { threw = true; }
    require(threw, "beat unit 3 throws");
}

// A pattern of plain notes: (start, key, duration) each, at 480 per beat.
Pattern pattern_of(Tick length, std::initializer_list<Sounding> notes) {
    Pattern pattern(length, 480);
    for (const Sounding& n : notes) {
        Trigger t;
        t.start = n.start;
        t.duration = n.duration;
        t.musical_data = Note{static_cast<std::int16_t>(n.key), 0.8F, 0.0F, 0.0};
        (void)pattern.add(t);
    }
    return pattern;
}

std::string shape(const Measure& measure) {
    // A compact reading of a measure: "D4:q. [C4 E4 G4]:q r:8" style.
    std::string out;
    const auto value = [](const NoteDuration& d) {
        std::string v;
        switch (d.value) {
            case NoteValue::Breve: v = "b"; break;
            case NoteValue::Whole: v = "w"; break;
            case NoteValue::Half: v = "h"; break;
            case NoteValue::Quarter: v = "q"; break;
            case NoteValue::Eighth: v = "8"; break;
            case NoteValue::Sixteenth: v = "16"; break;
            default: v = "s"; break;
        }
        v += std::string(static_cast<std::size_t>(d.dots), '.');
        if (d.tuplet()) v += "/" + std::to_string(d.tuplet_actual);
        return v;
    };
    for (std::size_t i = 0; i < measure.objects.size(); ++i) {
        const auto& object = measure.objects[i];
        if (const auto* rest = std::get_if<Rest>(&object)) {
            out += (out.empty() ? "" : " ") + std::string("r") + std::to_string(rest->voice) + ":" + value(rest->duration);
            continue;
        }
        const auto& note = std::get<NotationNote>(object);
        if (note.chord) {
            out += "+" + spelled_name(note.spelled);
        } else {
            out += (out.empty() ? "" : " ") + std::string(note.tie_end ? "~" : "") + std::to_string(note.voice) +
                   spelled_name(note.spelled);
        }
        const bool last_of_chord = i + 1 == measure.objects.size() ||
                                   !std::holds_alternative<NotationNote>(measure.objects[i + 1]) ||
                                   !std::get<NotationNote>(measure.objects[i + 1]).chord;
        if (last_of_chord) out += ":" + value(note.duration) + (note.tie_start ? "~" : "");
    }
    return out;
}

void round_trips(const Pattern& pattern, const Staff& staff, const std::string& what) {
    int dropped = -1;
    const Pattern back = to_pattern(staff, 480, &dropped);
    require(dropped == 0, what + ": nothing dropped");
    require(soundings_of(back) == soundings_of(pattern), what + ": every note comes back where it was");
}

void test_pattern_bridge() {
    // Scenario: A pattern becomes notation where its notes fall
    const Clef treble(ClefKind::Treble);
    {
        // A dotted-quarter D4, a C major triad, an eighth E4, then silence.
        Pattern pattern(1920, 480);
        Trigger melody;
        melody.duration = 720;
        melody.musical_data = Note{62, 0.9F, 0.0F, 0.0};
        (void)pattern.add(melody);
        Trigger chord;
        chord.start = 720;
        chord.duration = 480;
        Chord c;
        c.root = 60;
        c.intervals = {0, 4, 7};
        chord.musical_data = c;
        (void)pattern.add(chord);
        Trigger tail;
        tail.start = 1200;
        tail.duration = 240;
        tail.musical_data = Note{64, 0.8F, 0.0F, 0.0};
        (void)pattern.add(tail);
        const Staff staff = to_staff(pattern, KeySignature(2), treble, TimeSignature(4, 4));
        require(staff.measures.size() == 1, "one bar of 4/4");
        require(shape(staff.measures[0]) == "1D4:q. 1C4+E4+G4:q 1E4:8 r1:q",
                "notes, a chord and the closing rest, in time: " + shape(staff.measures[0]));
        const auto& first = std::get<NotationNote>(staff.measures[0].objects[0]);
        require(first.velocity == 0.9F, "velocity survives to_staff");
        round_trips(pattern, staff, "melody, chord and rest");
    }
    {
        // Scenario: A note off the notation grid across the barline comes back as one note
        // Regression: the rest before a note at 1760 is written 5 ticks
        // short (on the 128th grid) while the note's last 160 ticks of the
        // bar are an exact triplet eighth, so the tie stopped 5 ticks short of
        // the barline, missed the tied note there, and the note came back as
        // two, the second one starting at the barline.
        for (const Sounding& note : {Sounding{1760, 67, 320}, Sounding{1895, 67, 100}}) {
            const auto pattern = pattern_of(3840, {note});
            const Staff staff = to_staff(pattern, KeySignature(0), treble, TimeSignature(4, 4));
            const auto back = soundings_of(to_pattern(staff, 480));
            const std::string at = std::to_string(note.start);
            require(back.size() == 1, "the note at " + at + " tied over the barline is one note again, not " +
                                          std::to_string(back.size()));
            // Its start moves at most to the nearest 128th (15 ticks); its end stays.
            require(!back.empty() && std::abs(back[0].start - note.start) < 15 &&
                        back[0].start + back[0].duration == note.start + note.duration,
                    "the note at " + at + " starts on the nearest 128th and ends where it ended");
        }
    }
    {
        // A tie that never stops does not swallow a tied note bars later.
        NotationNote dangling;
        dangling.spelled = SpelledPitch{Step::C, Accidental::Natural, 4};
        dangling.duration = NoteDuration{NoteValue::Whole};
        dangling.tie_start = true;
        NotationNote late = dangling;
        late.tie_start = false;
        late.tie_end = true;
        Staff staff;
        Measure first;
        first.objects = {dangling};
        Measure empty;
        empty.objects = {Rest{NoteDuration{NoteValue::Whole}}};
        Measure third;
        third.objects = {late};
        staff.measures = {first, empty, third};
        require((soundings_of(to_pattern(staff, 480)) == std::vector<Sounding>{{0, 60, 1920}, {3840, 60, 1920}}),
                "a dangling tie leaves the C4 two bars later its own note");
    }
    {
        // Scenario: Silence between notes is written as rests
        const auto pattern = pattern_of(1920, {{0, 62, 480}, {960, 64, 480}});
        const Staff staff = to_staff(pattern, KeySignature(0), treble, TimeSignature(4, 4));
        require(shape(staff.measures[0]) == "1D4:q r1:q 1E4:q r1:q", "the gap is a rest: " + shape(staff.measures[0]));
        round_trips(pattern, staff, "gapped notes");
    }
    {
        // Scenario: A note across the barline is tied over it
        const auto pattern = pattern_of(3840, {{1440, 67, 960}});
        const Staff staff = to_staff(pattern, KeySignature(0), treble, TimeSignature(4, 4));
        require(staff.measures.size() == 2, "two bars");
        require(shape(staff.measures[0]) == "r1:h. 1G4:q~" && shape(staff.measures[1]) == "~1G4:q r1:h.",
                "tied over the barline: " + shape(staff.measures[0]) + " | " + shape(staff.measures[1]));
        round_trips(pattern, staff, "tie over the barline");
    }
    {
        // A tied chord, then a note after it: the note must not move.
        Pattern pattern(3840, 480);
        Trigger chord;
        chord.duration = 2880;
        Chord c;
        c.root = 60;
        c.intervals = {0, 4, 7};
        chord.musical_data = c;
        (void)pattern.add(chord);
        Trigger after;
        after.start = 2880;
        after.duration = 480;
        after.musical_data = Note{72, 0.8F, 0.0F, 0.0};
        (void)pattern.add(after);
        const Staff staff = to_staff(pattern, KeySignature(0), treble, TimeSignature(4, 4));
        require(shape(staff.measures[1]) == "~1C4+E4+G4:h 1C5:q r1:q",
                "the chord ties into bar two: " + shape(staff.measures[1]));
        round_trips(pattern, staff, "tied chord");
    }
    {
        // Scenario: A note held under moving ones gets its own voice
        const auto pattern =
            pattern_of(1920, {{0, 48, 1920}, {0, 64, 480}, {480, 65, 480}, {960, 67, 480}, {1440, 69, 480}});
        const Staff staff = to_staff(pattern, KeySignature(0), Clef(ClefKind::Bass), TimeSignature(4, 4));
        require(shape(staff.measures[0]) == "1C3:w 2E4:q 2F4:q 2G4:q 2A4:q",
                "held note in voice 1, the tune in voice 2: " + shape(staff.measures[0]));
        round_trips(pattern, staff, "two voices");
    }
    {
        // Scenario: A meter sets the bar length
        const auto pattern = pattern_of(4 * 1440, {{0, 60, 480}, {1440 * 3, 62, 1440}});
        const Staff staff = to_staff(pattern, KeySignature(0), treble, TimeSignature(3, 4));
        require(staff.measures.size() == 4, "four bars of 3/4");
        require(shape(staff.measures[1]) == "r1:h.", "an empty bar is one rest: " + shape(staff.measures[1]));
        require(to_pattern(staff, 480).length() == 4 * 1440, "the pattern comes back four bars of 3/4 long");
        round_trips(pattern, staff, "3/4");
    }
    {
        // Scenario: Triplets read as triplets
        const auto pattern = pattern_of(1920, {{0, 60, 160}, {160, 62, 160}, {320, 64, 160}});
        const Staff staff = to_staff(pattern, KeySignature(0), treble, TimeSignature(4, 4));
        require(shape(staff.measures[0]).starts_with("1C4:8/3 1D4:8/3 1E4:8/3"),
                "three eighth-note triplets: " + shape(staff.measures[0]));
        round_trips(pattern, staff, "triplets");
    }
    {
        // Retunes and velocities ride along; the key re-spells.
        Pattern pattern(1920, 480);
        Trigger quarter_tone;
        quarter_tone.duration = 480;
        quarter_tone.musical_data = Note{66, 0.5F, 0.0F, -50.0};
        (void)pattern.add(quarter_tone);
        const Staff flats = to_staff(pattern, KeySignature(-3), treble, TimeSignature(4, 4));
        const auto& note = std::get<NotationNote>(flats.measures[0].objects[0]);
        require(spelled_name(note.spelled) == "Gb4" && note.cents == -50.0, "Gb4 a quarter tone flat in Eb");
        const Pattern back = to_pattern(flats, 480);
        const auto& played = std::get<Note>(back.events().front().musical_data);
        require(played.key == 66 && played.cents == -50.0 && played.velocity == 0.5F,
                "the retune and the velocity come back");
    }
    {
        // Scenario: An unmetered staff is one bar as long as its notes
        const auto pattern = pattern_of(2400, {{0, 64, 480}, {960, 67, 480}, {1440, 69, 960}});
        const Staff staff = to_staff(pattern, KeySignature(0), treble, TimeSignature::unmetered());
        require(staff.measures.size() == 1, "chant is one measure");
        round_trips(pattern, staff, "unmetered");
    }

    // Scenario: A staff plays back with its transposition, pickup and changes
    NotationNote note;
    note.spelled = SpelledPitch{Step::D, Accidental::Natural, 4};
    note.duration = NoteDuration{NoteValue::Quarter};
    {
        Staff clarinet; // B-flat clarinet: written D4 sounds C4
        clarinet.transpose_semitones = -2;
        Measure bar;
        bar.objects = {note, Rest{NoteDuration{NoteValue::Half, 1}}};
        clarinet.measures = {bar};
        const Pattern back = to_pattern(clarinet, 480);
        require(std::get<Note>(back.events().front().musical_data).key == 60, "a B-flat clarinet's D4 sounds C4");
        Staff tenor = clarinet;
        tenor.transpose_semitones = 0;
        tenor.transpose_octaves = -1;
        require(std::get<Note>(to_pattern(tenor, 480).events().front().musical_data).key == 50,
                "treble-8vb sounds an octave down");
    }
    {
        Staff tune;
        Measure pickup;
        pickup.pickup = true;
        pickup.objects = {note};
        Measure bar;
        bar.objects = {note, Rest{NoteDuration{NoteValue::Half, 1}}};
        Measure waltz = bar;
        waltz.time = TimeSignature(3, 4);
        waltz.objects = {note, Rest{NoteDuration{NoteValue::Half}}};
        Measure after = waltz;
        after.time.reset();
        tune.measures = {pickup, bar, waltz, after};
        const auto sounds = soundings_of(to_pattern(tune, 480));
        require(sounds.size() == 4 && sounds[1].start == 480 && sounds[2].start == 480 + 1920 &&
                    sounds[3].start == 480 + 1920 + 1440,
                "the pickup lasts a beat, and 3/4 holds from where it is written");
    }
    {
        Staff high;
        Measure bar;
        NotationNote way_up = note;
        way_up.spelled.octave = 10;
        bar.objects = {way_up, note};
        high.measures = {bar};
        int dropped = 0;
        const Pattern back = to_pattern(high, 480, &dropped);
        require(dropped == 1 && back.events().size() == 1, "a note above MIDI 127 is left out and counted");
    }
}

void test_accidentals_in_the_bar() {
    // Scenario: An accidental holds to the end of its bar
    const auto pattern = pattern_of(3840, {{0, 66, 480}, {480, 65, 480}, {960, 65, 480}, {1440, 66, 960}, {2400, 65, 480}});
    const Staff staff = to_staff(pattern, KeySignature(0), Clef(ClefKind::Treble), TimeSignature(4, 4));
    std::vector<std::string> printed;
    for (const auto& placed : place(staff, 480)) {
        const auto* note = std::get_if<NotationNote>(&staff.measures[placed.measure].objects[placed.index]);
        if (note != nullptr) printed.push_back(spelled_name(note->spelled) + (placed.accidental_shown ? "!" : ""));
    }
    // F#, then F natural (printed), F again (the natural still holds), F#
    // tied over the bar (its tied end prints nothing), then F in bar two.
    require((printed == std::vector<std::string>{"F#4!", "F4!", "F4", "F#4!", "F#4", "F4!"}),
            "accidentals print by the bar's rule");
    Staff changed = staff;
    changed.measures[1].key = KeySignature(1);
    std::vector<bool> shown;
    for (const auto& placed : place(changed, 480))
        if (std::holds_alternative<NotationNote>(changed.measures[placed.measure].objects[placed.index]))
            shown.push_back(placed.accidental_shown);
    require(shown.size() == 6 && shown[5], "under G major in bar two, F natural prints its natural");
}

void test_placement() {
    // Scenario: Every object of a staff is placed where it sounds
    NotationNote quarter;
    quarter.spelled = SpelledPitch{Step::F, Accidental::Sharp, 4};
    quarter.duration = NoteDuration{NoteValue::Quarter};
    NotationNote held = quarter;
    held.spelled = SpelledPitch{Step::C, Accidental::Natural, 3};
    held.duration = NoteDuration{NoteValue::Whole};
    held.voice = 2;
    NotationNote third = quarter;
    third.spelled = SpelledPitch{Step::A, Accidental::Sharp, 4};
    third.chord = true;
    Staff staff;
    Measure pickup; // one beat
    pickup.pickup = true;
    pickup.objects = {quarter};
    Measure two_voices; // a quarter, a chord and a half over a held whole
    two_voices.objects = {quarter, quarter, third, Rest{NoteDuration{NoteValue::Half}}, held};
    Measure waltz; // 3/4, under a percussion clef
    waltz.time = TimeSignature(3, 4);
    waltz.clef = Clef(ClefKind::Percussion);
    waltz.objects = {quarter, Rest{NoteDuration{NoteValue::Half}}};
    Measure back_to_treble; // the 3/4 holds; the clef changes back
    back_to_treble.clef = Clef(ClefKind::Treble);
    back_to_treble.objects = {quarter, Rest{NoteDuration{NoteValue::Half}}};
    staff.measures = {pickup, two_voices, waltz, back_to_treble};

    struct Where {
        std::size_t measure, index;
        Tick onset, length;
        int voice;
        bool accidental;
        bool operator==(const Where&) const = default;
    };
    std::vector<Where> got;
    for (const Placed& p : place(staff, 480))
        got.push_back({p.measure, p.index, p.onset, p.length, p.voice, p.accidental_shown});
    // By measure, then onset, objects that start together in written order.
    // The pickup lasts a beat, so bar two starts at 480; the held C3 starts
    // there too, in its own voice; the chord's A#4 starts with its F#4. The
    // 3/4 bar starts a whole note later and lasts three beats. Under the
    // percussion clef nothing prints an accidental; back in treble it does.
    const std::vector<Where> expected = {
        {0, 0, 0, 480, 1, true},
        {1, 0, 480, 480, 1, true},   {1, 4, 480, 1920, 2, false}, {1, 1, 960, 480, 1, false},
        {1, 2, 960, 480, 1, true},   {1, 3, 1440, 960, 1, false},
        {2, 0, 2400, 480, 1, false}, {2, 1, 2880, 960, 1, false},
        {3, 0, 3840, 480, 1, true},  {3, 1, 4320, 960, 1, false},
    };
    require(got == expected, "objects are placed by voice, pickup, meter and clef");
    if (got != expected)
        for (const Where& w : got)
            std::cerr << "  m" << w.measure << " #" << w.index << " at " << w.onset << " for " << w.length << " v"
                      << w.voice << (w.accidental ? " !" : "") << "\n";
}

void test_lyrics() {
    // Scenario: A line of lyrics becomes one syllable per note, in any script
    require((split_lyrics("A-ma-zing grace  how sweet _ the\tsound") ==
             std::vector<std::string>{"A-", "ma-", "zing", "grace", "how", "sweet", "_", "the", "sound"}),
            "a line splits into syllables, words keeping their hyphens");
    const std::string hebrew = "\xD7\xA9\xD7\x81\xD6\xB8-\xD7\x9C\xD7\x95\xD6\xB9\xD7\x9D \xD7\xA2\xD6\xB2\xD7\x9C\xD6\xB5\xD7\x99\xD7\x9B\xD6\xB6\xD7\x9D";
    const auto syllables = split_lyrics(hebrew); // shalom aleichem, pointed
    require(syllables.size() == 3 && syllables[0].back() == '-', "Hebrew splits at its hyphen and space, points kept");
    require(join_lyrics(syllables) == hebrew, "and joins back to the same line");
    require(join_lyrics(split_lyrics("A-ma-zing grace")) == "A-ma-zing grace", "a word rejoins at its hyphens");

    // The syllable rides with its note through the staff and back, on the
    // chord's first note and the first piece of a tied note only.
    Pattern pattern(3840, 480);
    Trigger sung;
    sung.start = 1440;
    sung.duration = 960; // tied over the barline
    sung.musical_data = Note{67, 0.8F, 0.0F, 0.0};
    sung.lyric = syllables[0];
    (void)pattern.add(sung);
    Trigger dyad;
    dyad.start = 2400;
    dyad.duration = 480;
    Chord chord;
    chord.root = 60;
    chord.intervals = {0, 4};
    dyad.musical_data = chord;
    dyad.lyric = "grace";
    (void)pattern.add(dyad);
    const Staff staff = to_staff(pattern, KeySignature(0), Clef(ClefKind::Treble), TimeSignature(4, 4));
    std::vector<std::string> sung_lyrics;
    for (const auto& measure : staff.measures)
        for (const auto& object : measure.objects)
            if (const auto* note = std::get_if<NotationNote>(&object)) sung_lyrics.push_back(note->lyric);
    require((sung_lyrics == std::vector<std::string>{syllables[0], "", "grace", ""}),
            "the syllable is on the first piece of the tie and the chord's first note only");
    std::vector<std::string> back;
    // Held in a variable: a range-for over a temporary's events() reads a
    // pattern already destroyed (it passed on GCC and failed on macOS).
    const Pattern returned = to_pattern(staff, 480);
    for (const Trigger& t : returned.events()) back.push_back(t.lyric);
    require((back == std::vector<std::string>{syllables[0], "grace", ""}), "lyrics come back on their notes");

    // Through a MusicXML file and back, with the tempo.
    Score score;
    score.tempo_bpm = 72.0;
    score.parts.push_back({"Voice", {staff}});
    const Score read = parse(write(score));
    require(read.tempo_bpm && *read.tempo_bpm == 72.0, "the tempo comes back");
    std::string first_sung;
    for (const auto& object : read.parts[0].staves[0].measures[0].objects)
        if (const auto* note = std::get_if<NotationNote>(&object); note != nullptr && first_sung.empty()) first_sung = note->lyric;
    require(first_sung == syllables[0], "a Hebrew syllable comes back from MusicXML byte for byte");
}

void test_song_score() {
    // Scenario: The song is written as a score, its whole arrangement
    Song song;
    song.adopt_sections();
    require(!song.sections.empty() && !song.tracks.empty(), "a song has a section and a track");
    const std::size_t verse = 0;
    const auto part = song.part(verse, 0);
    require(part.has_value(), "the track has a part of the verse");
    auto& slot = song.patterns[*part];
    slot.pattern = Pattern(1920, 480);
    Trigger a;
    a.duration = 480;
    a.musical_data = Note{60, 0.8F, 0.0F, 0.0};
    a.lyric = "la";
    (void)slot.pattern.add(a);
    Trigger b = a;
    b.start = 960;
    b.musical_data = Note{62, 0.8F, 0.0F, 0.0};
    b.lyric = "";
    (void)slot.pattern.add(b);
    slot.clef = "Alto";
    song.sections[verse].key_fifths = 2;
    // Twice from bar 0, then once from bar 3 trimmed to start at its second
    // half, under 4/4 then 3/4 from bar 3.
    song.clips = {Clip{0, *part, 0, 2, 0, 0}, Clip{0, *part, 3 * 1920, 1, 0, 960}};
    song.meter.changes = {blokkily::MeterChange{0, 4, 4}, blokkily::MeterChange{3, 3, 4}};
    const Score score = song_score(song, "Round");
    require(score.parts.size() == 1 && score.parts[0].staves.size() == 1, "one part for the track with clips");
    const Staff& staff = score.parts[0].staves[0];
    require(staff.clef == Clef(ClefKind::Alto) && staff.key.fifths() == 2, "the part's clef and the section's key");
    require(staff.measures.size() == 4 && staff.measures[3].time && *staff.measures[3].time == TimeSignature(3, 4),
            "three bars of 4/4, then 3/4 where the meter changes");
    std::vector<std::pair<Tick, int>> played;
    const Pattern timeline = track_timeline(song, 0, 3 * 1920 + 1440);
    for (const Trigger& t : timeline.events())
        played.emplace_back(t.start, std::get<Note>(t.musical_data).key);
    require((played == std::vector<std::pair<Tick, int>>{{0, 60}, {960, 62}, {1920, 60}, {2880, 62}, {5760, 62}}),
            "every repeat, and the trimmed clip from its second half");
    const auto replayed = to_pattern(staff, 480);
    require(replayed.events().size() == 5 && replayed.events().front().lyric == "la", "the score plays the timeline, lyrics and all");
}

void test_add_score() {
    // Scenario: A score comes into the song as a section of its own
    Score score;
    score.title = "Hymn";
    score.tempo_bpm = 66.0;
    Staff upper;
    upper.key = KeySignature(-1);
    upper.time = TimeSignature(3, 4);
    Measure bar;
    NotationNote note;
    note.spelled = SpelledPitch{Step::F, Accidental::Natural, 4};
    note.duration = NoteDuration{NoteValue::Half, 1};
    note.lyric = "Ho-";
    bar.objects = {note};
    upper.measures = {bar, bar};
    Staff lower = upper;
    lower.clef = Clef(ClefKind::Bass);
    for (auto& measure : lower.measures) std::get<NotationNote>(measure.objects[0]).spelled.octave = 2;
    score.parts.push_back({"Choir", {upper, lower}});

    Song fresh;
    fresh.adopt_sections();
    const auto landing = add_score(fresh, score);
    require(landing.fresh && landing.tracks.size() == 2 && landing.notes == 4, "two staves land as two tracks of notes");
    require(fresh.tracks[landing.tracks[0]].name == "Choir 1" && fresh.tracks[landing.tracks[1]].name == "Choir 2",
            "the tracks are named after the part");
    require(fresh.patterns[*fresh.part(landing.section, landing.tracks[1])].clef == "Bass", "the lower staff keeps its bass clef");
    require(fresh.sections[landing.section].key_fifths == -1 && fresh.sections[landing.section].meter_beats == 3,
            "the section takes the key and the meter");
    require(fresh.meter.changes.size() == 1 && fresh.meter.changes[0].numerator == 3 &&
                fresh.tempo.points.front().bpm == 66.0,
            "a song with no notes takes the score's meter and tempo");
    require(fresh.patterns[*fresh.part(landing.section, landing.tracks[0])].pattern.length() == 2 * 1440,
            "the section is two bars of 3/4 long");
    require(fresh.patterns[*fresh.part(landing.section, landing.tracks[0])].pattern.events().front().lyric == "Ho-",
            "lyrics land on their notes");
    bool placed = false;
    for (const auto& clip : fresh.clips) placed = placed || (clip.start == 0 && fresh.patterns[clip.pattern].section == landing.section);
    require(placed, "its clips start the arrangement");

    // Into a song that has notes: new tracks, after the last clip, and the
    // song's own meter and tempo are left alone.
    Song busy = fresh;
    const auto tracks_before = busy.tracks.size();
    const auto second = add_score(busy, score);
    require(!second.fresh && busy.tracks.size() == tracks_before + 2, "a song with notes gets new tracks");
    require(second.start == 2 * 1440 && busy.tempo.points.front().bpm == 66.0, "the clips go after the last one");

    // Regression: a song holding only a recorded vocal was taken for an
    // empty one - its track renamed and given notes, its tempo replaced and
    // the score's clips laid over the recording.
    Song vocal;
    vocal.adopt_sections();
    vocal.tracks[0].name = "VOCAL";
    vocal.audio_files.push_back({"/take.wav", 48000 * 4, 48000, 1});
    AudioClip take;
    take.id = 1;
    take.track = 0;
    take.length_frames = 48000 * 4; // four seconds: eight beats at 120
    vocal.audio_clips.push_back(take);
    const auto over_vocal = add_score(vocal, score);
    require(!over_vocal.fresh && vocal.tracks[0].name == "VOCAL" &&
                std::find(over_vocal.tracks.begin(), over_vocal.tracks.end(), 0U) == over_vocal.tracks.end(),
            "a song with recorded audio keeps its tracks: the score gets new ones");
    require(vocal.tempo.points.front().bpm == 120.0 && over_vocal.start >= 8 * 480,
            "its tempo stays, and the score's clips go after the recording");

    // Regression: a multi-output channel or a frozen track is not lent to a
    // part, even in a song with no notes.
    Song channels;
    channels.adopt_sections();
    channels.tracks.push_back(channels.tracks[0]);
    channels.tracks.push_back(channels.tracks[0]);
    channels.add_track_parts(1);
    channels.add_track_parts(2);
    channels.tracks[1].source = InstrumentOutput{0, 2};
    channels.tracks[2].frozen = true;
    const auto into_channels = add_score(channels, score);
    require((into_channels.tracks == std::vector<std::size_t>{0, 3}) && channels.tracks[1].source &&
                channels.tracks[1].name == "Track" && channels.tracks[2].name == "Track",
            "the score takes the track that plays its own instrument, and adds the rest");

    // Regression: a long title in Hebrew was cut inside a character.
    Score titled = score;
    titled.title = "\xD7\xA9\xD7\x9C\xD7\x95\xD7\x9D \xD7\xA2\xD7\x9C\xD7\x99\xD7\x9B\xD7\x9D "
                   "\xD7\x9E\xD7\x9C\xD7\x90\xD7\x9B\xD7\x99 \xD7\x94\xD7\xA9\xD7\xA8\xD7\xAA";
    titled.title = "AB " + titled.title + " " + titled.title; // the 24th byte falls inside a letter
    Song named;
    named.adopt_sections();
    const auto named_landing = add_score(named, titled);
    const std::string& kept = named.sections[named_landing.section].name;
    std::size_t characters = 0;
    for (const char c : kept) characters += (static_cast<unsigned char>(c) & 0xC0U) != 0x80U;
    const bool whole = !kept.empty() && titled.title.starts_with(kept) &&
                       (static_cast<unsigned char>(titled.title[kept.size()]) & 0xC0U) != 0x80U;
    require(characters <= 24 && whole, "a Hebrew title is cut between characters");
}

} // namespace

int main() {
    test_pitch();
    test_clefs();
    test_key_signature();
    test_spelling();
    test_durations();
    test_layout();
    test_pattern_bridge();
    test_accidentals_in_the_bar();
    test_placement();
    test_lyrics();
    test_song_score();
    test_add_score();
    if (failures == 0) {
        std::cout << "notation model: all tests passed\n";
        return 0;
    }
    std::cerr << "notation model: " << failures << " failure(s)\n";
    return 1;
}
