// Staff notation model tests (task A of the staff-notation project).
//
// features/notation.feature: each case below names the scenario it proves.
// The model scenarios are proved here; the score-view scenarios are task C's,
// proved by the screenshot gate.

#include "blokkily/notation/clef.hpp"
#include "blokkily/notation/duration.hpp"
#include "blokkily/notation/layout.hpp"
#include "blokkily/notation/note.hpp"
#include "blokkily/notation/pattern_bridge.hpp"
#include "blokkily/notation/pitch.hpp"
#include "blokkily/notation/signature.hpp"
#include "blokkily/notation/spelling.hpp"
#include "blokkily/notation/staff.hpp"

#include <algorithm>
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

void test_pattern_bridge() {
    // Scenario: A pattern renders to spelled, tied notation and replays back
    constexpr Tick tpb = 480;
    Pattern pattern(1920, tpb);
    Trigger melody;
    melody.start = 0;
    melody.duration = 720; // dotted quarter
    melody.musical_data = Note{62, 0.9F, 0.0F, 0.0}; // D4
    (void)pattern.add(melody);
    Trigger chord;
    chord.start = 720;
    chord.duration = 480;
    Chord c;
    c.root = 60;
    c.intervals = {0, 4, 7};
    c.velocity = 0.7F;
    chord.musical_data = c;
    (void)pattern.add(chord);

    const KeySignature key(2); // D major
    const Staff staff = to_staff(pattern, key, Clef(ClefKind::Treble), tpb);
    require(staff.measures.size() == 1, "one measure");
    require(staff.measures[0].objects.size() == 4, "dotted quarter + three chord voices");
    const auto* first = std::get_if<NotationNote>(&staff.measures[0].objects[0]);
    require(first != nullptr && spelled_name(first->spelled) == "D4", "D4 spelled in D major");
    require(first->duration.dots == 1 && first->duration.value == NoteValue::Quarter,
            "720 ticks is a dotted quarter");
    require(!first->tie_start && !first->tie_end, "single value ties neither way");
    require(first->velocity == 0.9F, "velocity survives to_staff");
    // The chord's third voice is A4; check one tied chain while here.
    Pattern tied_pattern(1920, tpb);
    Trigger odd;
    odd.start = 0;
    odd.duration = 2880; // whole + half
    odd.musical_data = Note{69, 0.8F, 0.0F, 0.0};
    (void)tied_pattern.add(odd);
    const Staff tied_staff = to_staff(tied_pattern, KeySignature(0), Clef(ClefKind::Treble), tpb);
    require(tied_staff.measures[0].objects.size() == 2, "2880 ticks ties two notes");
    const auto* t0 = std::get_if<NotationNote>(&tied_staff.measures[0].objects[0]);
    const auto* t1 = std::get_if<NotationNote>(&tied_staff.measures[0].objects[1]);
    require(t0->tie_start && !t0->tie_end && !t1->tie_start && t1->tie_end,
            "tie flags chain the two notes");

    // Round trip: every sounding voice comes back at the same tick, key, length.
    const Pattern back = to_pattern(staff, tpb);
    require(soundings_of(back) == soundings_of(pattern), "pattern round-trips voices");
    const Pattern tied_back = to_pattern(tied_staff, tpb);
    require(soundings_of(tied_back) == soundings_of(tied_pattern),
            "tied durations round-trip");

    // Transposition: a treble-8vb staff sounds an octave below the writing.
    Staff low = staff;
    low.transpose_octaves = -1;
    const Pattern low_back = to_pattern(low, tpb);
    for (const Trigger& t : low_back.events()) {
        const auto* n = std::get_if<Note>(&t.musical_data);
        require(n != nullptr && n->key == 62 - 12, "treble-8vb sounds an octave down");
        break;
    }

    // Rests advance the cursor; unmetered staves pack notes end to end.
    Staff chant;
    chant.clef = Clef(ClefKind::Treble);
    chant.time = TimeSignature::unmetered();
    Measure m;
    NotationNote n1;
    n1.spelled = SpelledPitch{Step::E, Accidental::Natural, 4};
    n1.duration = NoteDuration{NoteValue::Quarter};
    NotationNote n2 = n1;
    n2.spelled = SpelledPitch{Step::G, Accidental::Natural, 4};
    n2.lyric = "a-";
    m.objects.emplace_back(n1);
    m.objects.emplace_back(Rest{NoteDuration{NoteValue::Quarter}});
    m.objects.emplace_back(n2);
    chant.measures.push_back(m);
    const Pattern chant_back = to_pattern(chant, tpb);
    const auto chant_sounds = soundings_of(chant_back);
    require(chant_sounds.size() == 2, "two notes around the rest");
    require(chant_sounds[0] == Sounding{0, 64, 480}, "first chant note at 0");
    require(chant_sounds[1] == Sounding{960, 67, 480}, "second chant note after the rest");
}

void test_score_hierarchy() {
    // Scenario: Parts group staves under a titled score
    Score score;
    score.title = "Test Concerto";
    score.composer = "A. Fader";
    Part piano;
    piano.name = "Piano";
    Staff right;
    right.clef = Clef(ClefKind::Treble);
    Staff left;
    left.clef = Clef(ClefKind::Bass);
    piano.staves = {right, left};
    score.parts.push_back(piano);
    require(score.parts.size() == 1 && score.parts[0].staves.size() == 2,
            "piano part is a grand staff");
    require(score.parts[0].staves[1].clef == Clef(ClefKind::Bass),
            "left hand is bass clef");
    require(score.title == "Test Concerto", "title survives");
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
    test_score_hierarchy();
    if (failures == 0) {
        std::cout << "notation model: all tests passed\n";
        return 0;
    }
    std::cerr << "notation model: " << failures << " failure(s)\n";
    return 1;
}
