// Executable scenarios for features/musicxml.feature: MusicXML 4.0 partwise
// import/export of notation scores. Each block names the scenario it proves.
//
// The export scenarios follow the repo's export rule: the written document is
// read back and compared with what the engine renders — the re-imported score
// is converted with notation::to_pattern and the scheduled notes are compared
// against the original score's.

#include "blokkily/notation/musicxml.hpp"
#include "blokkily/notation/pattern_bridge.hpp"
#include "blokkily/notation/staff.hpp"
#include "blokkily/sequencer/scheduler.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

using namespace blokkily;
using namespace blokkily::notation;

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    if (condition) return;
    std::cerr << "FAILED: " << what << '\n';
    ++failures;
}

NotationNote pitched(Step step, int alter, int octave, NoteValue value, int dots = 0) {
    NotationNote note;
    note.spelled = SpelledPitch{step, static_cast<Accidental>(alter), octave};
    note.duration = NoteDuration{value, dots};
    return note;
}

// One loop of the pattern the engine would render, as sorted key/start/duration
// triples. This is the "what the engine renders" side of the export rule.
std::vector<std::tuple<int, Tick, Tick>> rendered_notes(const Pattern& pattern) {
    const auto scheduled = Scheduler{}.render_loop(pattern, 0, 0);
    std::vector<std::tuple<int, Tick, Tick>> notes;
    for (const auto& note : scheduled)
        notes.emplace_back(note.key, note.start, note.duration);
    std::sort(notes.begin(), notes.end());
    return notes;
}

// A single-staff score plays through one pattern; the round trip must render
// the same notes.
void check_bridge_round_trip(const Score& score, Tick ticks_per_beat, const char* what) {
    check(!score.parts.empty() && !score.parts.front().staves.empty(), what);
    if (score.parts.empty() || score.parts.front().staves.empty()) return;
    const Pattern original = to_pattern(score.parts.front().staves.front(), ticks_per_beat);
    const Score reparsed = parse(write(score));
    const Pattern through_xml =
        to_pattern(reparsed.parts.front().staves.front(), ticks_per_beat);
    check(rendered_notes(original) == rendered_notes(through_xml), what);
}

const Clef all_clefs[] = {
    Clef(ClefKind::FrenchViolin), Clef(ClefKind::Treble), Clef(ClefKind::Subbass),
    Clef(ClefKind::Bass), Clef(ClefKind::BaritoneF), Clef(ClefKind::Soprano),
    Clef(ClefKind::MezzoSoprano), Clef(ClefKind::Alto), Clef(ClefKind::Tenor),
    Clef(ClefKind::BaritoneC), Clef(ClefKind::Percussion),
};

} // namespace

int main() {
    // features/musicxml.feature: exporting every clef in the roster
    {
        Score score;
        score.title = "Clef parade";
        score.composer = "Test";
        Part part;
        part.name = "All clefs";
        for (const auto clef : all_clefs) {
            Staff staff;
            staff.clef = clef;
            Measure measure;
            measure.objects.push_back(pitched(Step::C, 0, 4, NoteValue::Quarter));
            staff.measures.push_back(std::move(measure));
            part.staves.push_back(std::move(staff));
        }
        score.parts.push_back(std::move(part));

        const std::string xml = write(score);
        check(xml.find("<sign>percussion</sign>") != std::string::npos,
              "clefs: percussion sign exported");
        check(xml.find("<unpitched>") != std::string::npos,
              "clefs: percussion note exported as unpitched");

        const Score back = parse(xml);
        check(back.title == "Clef parade", "clefs: title round-trips");
        check(back.composer == "Test", "clefs: composer round-trips");
        check(back.parts.size() == 1 && back.parts[0].name == "All clefs",
              "clefs: part name round-trips");
        check(back.parts[0].staves.size() == 11, "clefs: all staves round-trip");
        for (std::size_t i = 0; i < 11; ++i)
            check(back.parts[0].staves[i].clef == all_clefs[i], "clefs: clef round-trips");
        const auto& perc =
            std::get<NotationNote>(back.parts[0].staves[10].measures[0].objects[0]);
        check(perc.spelled.step == Step::C && perc.spelled.octave == 4,
              "clefs: unpitched note reads back as C4");
    }

    // features/musicxml.feature: key signatures from seven flats to seven sharps
    {
        Score score;
        Part part;
        part.name = "keys";
        for (int fifths = -7; fifths <= 7; ++fifths) {
            Staff staff;
            staff.key = KeySignature{fifths};
            staff.time = TimeSignature{3, 4};
            Measure measure;
            measure.objects.push_back(pitched(Step::G, 0, 4, NoteValue::Half));
            staff.measures.push_back(std::move(measure));
            part.staves.push_back(std::move(staff));
        }
        score.parts.push_back(std::move(part));

        const Score back = parse(write(score));
        check(back.parts[0].staves.size() == 15, "keys: fifteen staves round-trip");
        for (int i = 0; i < 15; ++i)
            check(back.parts[0].staves[static_cast<std::size_t>(i)].key.fifths() == i - 7,
                  "keys: fifths round-trip");
        check(back.parts[0].staves[0].time.beats() == 3, "keys: time signature kept");
    }

    // features/musicxml.feature: accidentals, dots, ties, voices, stemless
    {
        Score score;
        Part part;
        part.name = "solo";
        Staff staff;
        staff.key = KeySignature{2};
        staff.time = TimeSignature{6, 8};
        Measure measure;
        auto a = pitched(Step::F, 1, 5, NoteValue::Eighth, 1);
        a.tie_start = true;
        auto b = pitched(Step::F, 1, 5, NoteValue::Eighth, 1);
        b.tie_start = true;
        b.tie_end = true;
        auto c = pitched(Step::B, -2, 3, NoteValue::Sixteenth, 2);
        c.tie_end = true;
        c.voice = 2;
        c.duration.stemless = true;
        auto d = pitched(Step::E, 2, 6, NoteValue::HundredTwentyEighth, 3);
        measure.objects.push_back(a);
        measure.objects.push_back(b);
        measure.objects.push_back(c);
        measure.objects.push_back(d);
        Rest rest;
        rest.duration.value = NoteValue::Quarter;
        measure.objects.push_back(rest);
        staff.measures.push_back(std::move(measure));
        part.staves.push_back(std::move(staff));
        score.parts.push_back(std::move(part));

        const std::string xml = write(score);
        check(xml.find("<alter>2</alter>") != std::string::npos,
              "notes: double sharp exported");
        check(xml.find("<alter>-2</alter>") != std::string::npos,
              "notes: double flat exported");
        check(xml.find("<stem>none</stem>") != std::string::npos,
              "notes: stemless exported");

        const Score back = parse(xml);
        const auto& notes = back.parts[0].staves[0].measures[0].objects;
        std::vector<const NotationNote*> voice1, voice2;
        int rests = 0;
        for (const auto& event : notes) {
            if (std::holds_alternative<Rest>(event)) {
                ++rests;
            } else if (std::get<NotationNote>(event).voice == 1) {
                voice1.push_back(&std::get<NotationNote>(event));
            } else {
                voice2.push_back(&std::get<NotationNote>(event));
            }
        }
        check(voice1.size() == 3 && voice2.size() == 1 && rests == 1,
              "notes: voices regroup, rests kept");
        check(voice1[0]->tie_start && !voice1[0]->tie_end, "notes: tie start kept");
        check(voice1[0]->spelled.accidental == Accidental::Sharp, "notes: sharp kept");
        check(voice1[1]->tie_start && voice1[1]->tie_end, "notes: tie chain kept");
        check(voice2[0]->tie_end && voice2[0]->duration.stemless && voice2[0]->voice == 2,
              "notes: tie end, stemless, voice kept");
        check(voice2[0]->spelled.accidental == Accidental::DoubleFlat,
              "notes: double flat kept");
        check(voice1[2]->duration.value == NoteValue::HundredTwentyEighth &&
                  voice1[2]->duration.dots == 3,
              "notes: triple-dotted 128th kept");
        check(voice1[2]->spelled.accidental == Accidental::DoubleSharp,
              "notes: double sharp kept");

        check_bridge_round_trip(score, 480, "notes: re-imported score renders the same");
    }

    // features/musicxml.feature: lyrics, melisma and extender lines
    {
        Score score;
        Part part;
        Staff staff;
        Measure measure;
        auto l1 = pitched(Step::C, 0, 4, NoteValue::Quarter);
        l1.lyric = "A";
        auto l2 = pitched(Step::D, 0, 4, NoteValue::Quarter);
        l2.lyric = "-";
        auto l3 = pitched(Step::E, 0, 4, NoteValue::Quarter);
        l3.lyric = "-";
        auto l4 = pitched(Step::F, 0, 4, NoteValue::Quarter);
        l4.lyric = "men";
        auto l5 = pitched(Step::G, 0, 4, NoteValue::Quarter);
        l5.lyric = "_";
        auto l6 = pitched(Step::A, 0, 4, NoteValue::Quarter);
        l6.lyric = "plain";
        measure.objects.push_back(l1);
        measure.objects.push_back(l2);
        measure.objects.push_back(l3);
        measure.objects.push_back(l4);
        measure.objects.push_back(l5);
        measure.objects.push_back(l6);
        staff.measures.push_back(std::move(measure));
        part.staves.push_back(std::move(staff));
        score.parts.push_back(std::move(part));

        const std::string xml = write(score);
        check(xml.find("<syllabic>begin</syllabic>") != std::string::npos,
              "lyrics: melisma begin exported");
        check(xml.find("<syllabic>end</syllabic>") != std::string::npos,
              "lyrics: melisma end exported");
        check(xml.find("<extend") != std::string::npos, "lyrics: extender exported");

        const Score back = parse(xml);
        const auto& notes = back.parts[0].staves[0].measures[0].objects;
        const char* expected[] = {"A", "-", "-", "men", "_", "plain"};
        check(notes.size() == 6, "lyrics: six notes round-trip");
        for (std::size_t i = 0; i < 6 && i < notes.size(); ++i)
            check(std::get<NotationNote>(notes[i]).lyric == expected[i],
                  "lyrics: syllable round-trips");
    }

    // features/musicxml.feature: parts, grand staff, measures, transposition,
    // unmetered chant
    {
        Score score;
        score.title = "T";
        score.composer = "C";
        Part piano;
        piano.name = "Piano";
        Staff right, left;
        right.clef = Clef(ClefKind::Treble);
        left.clef = Clef(ClefKind::Bass);
        for (int i = 0; i < 2; ++i) {
            Measure up, down;
            up.objects.push_back(pitched(Step::C, 0, 5, NoteValue::Quarter));
            down.objects.push_back(pitched(Step::C, 0, 2, NoteValue::Quarter));
            right.measures.push_back(std::move(up));
            left.measures.push_back(std::move(down));
        }
        piano.staves.push_back(std::move(right));
        piano.staves.push_back(std::move(left));
        Part chant;
        chant.name = "Chant";
        Staff chant_staff;
        chant_staff.clef = Clef(ClefKind::Alto);
        chant_staff.time = TimeSignature::unmetered();
        chant_staff.transpose_octaves = -1;
        Measure chant_measure;
        auto chant_note = pitched(Step::D, 0, 4, NoteValue::Whole);
        chant_note.duration.stemless = true;
        chant_measure.objects.push_back(chant_note);
        chant_staff.measures.push_back(std::move(chant_measure));
        chant.staves.push_back(std::move(chant_staff));
        score.parts.push_back(std::move(piano));
        score.parts.push_back(std::move(chant));

        const std::string xml = write(score);
        check(xml.find("senza-misura") != std::string::npos,
              "chant: senza-misura exported");
        check(xml.find("<octave-change>-1</octave-change>") != std::string::npos,
              "chant: octave transposition exported");
        check(xml.find("<staves>2</staves>") != std::string::npos,
              "piano: two staves exported");
        check(xml.find("<backup>") != std::string::npos,
              "piano: staves rewound with backup");

        const Score back = parse(xml);
        check(back.parts.size() == 2, "parts: two parts round-trip");
        check(back.parts[0].staves.size() == 2, "piano: grand staff round-trips");
        check(back.parts[0].staves[0].measures.size() == 2, "piano: measures kept");
        check(back.parts[0].staves[1].clef == Clef(ClefKind::Bass), "piano: bass clef kept");
        const auto& up =
            std::get<NotationNote>(back.parts[0].staves[0].measures[0].objects[0]);
        check(up.spelled.octave == 5, "piano: right-hand pitch kept");
        check(back.parts[1].staves[0].time.metered() == false, "chant: unmetered kept");
        check(back.parts[1].staves[0].transpose_octaves == -1, "chant: transposition kept");
        const auto& cn =
            std::get<NotationNote>(back.parts[1].staves[0].measures[0].objects[0]);
        check(cn.duration.stemless && cn.duration.value == NoteValue::Whole, "chant: stemless whole kept");
    }

    // features/musicxml.feature: foreign files and malformed input
    {
        Score score;
        Part part;
        Staff staff;
        Measure measure;
        measure.objects.push_back(pitched(Step::C, 0, 4, NoteValue::Quarter));
        staff.measures.push_back(std::move(measure));
        part.staves.push_back(std::move(staff));
        score.parts.push_back(std::move(part));

        std::string xml = write(score);
        const auto attrs = xml.find("</attributes>");
        check(attrs != std::string::npos, "foreign: attributes block found");
        xml.insert(attrs, "<direction><direction-type><words>Allegro</words></"
                          "direction-type></direction>"
                          "<harmony><root><root-step>C</root-step></root></harmony>");
        const Score back = parse(xml); // unknown elements must not fail
        check(back.parts[0].staves.size() == 1, "foreign: unknown elements skipped");

        bool threw = false;
        try {
            (void)parse("<score-partwise><part>");
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "foreign: malformed XML throws");

        threw = false;
        try {
            (void)parse("<score-timewise version=\"4.0\"/>");
        } catch (const std::runtime_error&) {
            threw = true;
        }
        check(threw, "foreign: timewise documents rejected");

        std::string odd_clef = write(score);
        const auto sign = odd_clef.find("<sign>G</sign>");
        check(sign != std::string::npos, "foreign: clef sign found");
        odd_clef.replace(sign, 14, "<sign>weird</sign>");
        const Score kept = parse(odd_clef);
        check(kept.parts[0].staves[0].clef == Clef(ClefKind::Treble),
              "foreign: unknown clef keeps the staff clef");
    }

    if (failures == 0) std::cout << "musicxml: all scenarios passed\n";
    return failures == 0 ? 0 : 1;
}
