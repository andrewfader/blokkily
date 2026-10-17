// Executable scenarios for features/musicxml.feature: MusicXML 4.0 partwise
// import/export of notation scores. Each block names the scenario it proves.
//
// The export scenarios follow the repo's export rule: the document is written
// to a file, the file is read back, and what the scheduler plays from the
// re-imported score is compared with what it plays from the pattern that was
// exported - start, length, key, retune and velocity of every note.

#include "blokkily/notation/musicxml.hpp"
#include "blokkily/notation/pattern_bridge.hpp"
#include "blokkily/notation/staff.hpp"
#include "blokkily/sequencer/scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

using namespace blokkily;
using namespace blokkily::notation;

namespace {

int failures = 0;

void check(bool condition, const std::string& what) {
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

// One loop of the pattern as the scheduler plays it.
using Played = std::tuple<Tick, int, Tick, long long, int>; // start, key, length, cents x1000, velocity x1000
std::vector<Played> rendered_notes(const Pattern& pattern) {
    std::vector<Played> notes;
    for (const auto& note : Scheduler{}.render_loop(pattern, 0, 0))
        notes.emplace_back(note.start, note.key, note.duration, std::llround(note.cents * 1000.0),
                           static_cast<int>(std::lround(note.velocity * 1000.0F)));
    std::sort(notes.begin(), notes.end());
    return notes;
}

// The document as a file on disk, read back from that file.
Score through_file(const Score& score, const std::string& name) {
    const auto path = std::filesystem::temp_directory_path() / ("blokkily-musicxml-" + name + ".musicxml");
    {
        std::ofstream out(path, std::ios::binary);
        out << write(score);
    }
    std::ifstream in(path, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    std::filesystem::remove(path);
    return parse(text.str());
}

Score score_of(Staff staff, const std::string& name = "Part") {
    Score score;
    Part part;
    part.name = name;
    part.staves.push_back(std::move(staff));
    score.parts.push_back(std::move(part));
    return score;
}

void add(Pattern& pattern, Tick start, Tick length, int key, float velocity = 0.8F, double cents = 0.0) {
    Trigger trigger;
    trigger.start = start;
    trigger.duration = length;
    trigger.musical_data = Note{static_cast<std::int16_t>(key), velocity, 0.0F, cents};
    (void)pattern.add(trigger);
}

template <typename Error>
bool throws(const std::string& xml) {
    try {
        (void)parse(xml);
    } catch (const Error&) {
        return true;
    } catch (...) {
        return false;
    }
    return false;
}

std::string document(const std::string& attributes, const std::string& notes) {
    return "<?xml version=\"1.0\"?><score-partwise version=\"4.0\"><part-list>"
           "<score-part id=\"P1\"><part-name>T</part-name></score-part></part-list>"
           "<part id=\"P1\"><measure number=\"1\"><attributes>" +
           attributes + "</attributes>" + notes + "</measure></part></score-partwise>";
}

const Clef all_clefs[] = {
    Clef(ClefKind::FrenchViolin), Clef(ClefKind::Treble), Clef(ClefKind::Subbass),
    Clef(ClefKind::Bass), Clef(ClefKind::BaritoneF), Clef(ClefKind::Soprano),
    Clef(ClefKind::MezzoSoprano), Clef(ClefKind::Alto), Clef(ClefKind::Tenor),
    Clef(ClefKind::BaritoneC), Clef(ClefKind::Percussion),
};

} // namespace

int main() {
    // Scenario: An exported pattern plays back the same from the file
    {
        Pattern pattern(4 * 1920, 480);
        add(pattern, 0, 720, 62, 0.9F);                 // dotted quarter
        add(pattern, 720, 480, 60);                      // a triad...
        add(pattern, 720, 480, 64);
        add(pattern, 720, 480, 67);
        add(pattern, 1440, 960, 72, 0.6F);               // tied over the barline
        add(pattern, 1920, 1920 * 2, 48, 0.5F);          // held two bars under...
        add(pattern, 2400, 480, 76);                     // ...moving notes
        add(pattern, 2880, 160, 74, 0.7F, -50.0);        // a triplet, a quarter tone flat
        add(pattern, 3040, 160, 72);
        add(pattern, 3200, 160, 71, 0.8F, 31.6);
        add(pattern, 5760, 1920, 66);                    // the last bar, after a gap
        const Staff staff = to_staff(pattern, KeySignature(2), Clef(ClefKind::Treble), TimeSignature(4, 4));
        const Score back = through_file(score_of(staff), "pattern");
        check(back.parts.size() == 1 && back.parts[0].staves.size() == 1, "export: one part, one staff");
        int dropped = -1;
        const Pattern replayed = to_pattern(back.parts[0].staves[0], 480, &dropped);
        check(dropped == 0, "export: nothing dropped on the way back");
        if (rendered_notes(replayed) != rendered_notes(pattern))
            for (const auto& list : {rendered_notes(pattern), rendered_notes(replayed)}) {
                for (const auto& [start, key, length, cents, velocity] : list)
                    std::cerr << start << "/" << key << "/" << length << "/" << cents << "/" << velocity << " ";
                std::cerr << '\n';
            }
        check(rendered_notes(replayed) == rendered_notes(pattern),
              "export: the file plays every note at its time, length, key, retune and velocity");
        check(replayed.length() == pattern.length(), "export: the pattern comes back four bars long");
    }

    // Scenario: Exporting every clef in the roster
    {
        Score score;
        score.title = "Clef parade";
        score.composer = "Test";
        Part part;
        part.name = "All clefs";
        for (const auto& clef : all_clefs) {
            Staff staff;
            staff.clef = clef;
            Measure measure;
            measure.objects.emplace_back(pitched(Step::C, 0, 4, NoteValue::Whole));
            staff.measures.push_back(measure);
            part.staves.push_back(staff);
        }
        score.parts.push_back(part);
        const Score back = through_file(score, "clefs");
        check(back.title == "Clef parade" && back.composer == "Test", "clefs: title and composer kept");
        check(back.parts.size() == 1 && back.parts[0].staves.size() == std::size(all_clefs), "clefs: every staff kept");
        for (std::size_t s = 0; s < std::size(all_clefs) && s < back.parts[0].staves.size(); ++s)
            check(back.parts[0].staves[s].clef == all_clefs[s],
                  "clefs: " + std::string(all_clefs[s].name()) + " comes back");
    }

    // Scenario: Key signatures from seven flats to seven sharps
    for (int fifths = -7; fifths <= 7; ++fifths) {
        Staff staff;
        staff.key = KeySignature(fifths);
        Measure measure;
        measure.objects.emplace_back(pitched(Step::F, 1, 4, NoteValue::Whole));
        staff.measures.push_back(measure);
        const Score back = through_file(score_of(staff), "key");
        check(back.parts[0].staves[0].key.fifths() == fifths, "keys: " + std::to_string(fifths) + " fifths kept");
    }

    // Scenario: Lyrics keep their words and their held syllables
    {
        Staff staff;
        Measure first;
        Measure second;
        const char* words[] = {"Al-", "le-", "lu-", "ia"};
        for (const char* syllable : words) {
            auto note = pitched(Step::G, 0, 4, NoteValue::Quarter);
            note.lyric = syllable;
            first.objects.emplace_back(note);
        }
        const char* held[] = {"men", "_", "_"};
        for (const char* syllable : held) {
            auto note = pitched(Step::A, 0, 4, NoteValue::Quarter);
            note.lyric = syllable;
            second.objects.emplace_back(note);
        }
        second.objects.emplace_back(Rest{NoteDuration{NoteValue::Quarter}});
        staff.measures = {first, second};
        const std::string xml = write(score_of(staff));
        check(xml.find("<syllabic>begin</syllabic>") != std::string::npos &&
                  xml.find("<syllabic>middle</syllabic>") != std::string::npos &&
                  xml.find("<syllabic>end</syllabic>") != std::string::npos,
              "lyrics: a hyphenated word is begin, middle, end");
        check(xml.find("<extend type=\"start\"") != std::string::npos &&
                  xml.find("<extend type=\"continue\"") != std::string::npos &&
                  xml.find("<extend type=\"stop\"") != std::string::npos,
              "lyrics: a held syllable starts, continues and stops its line");
        const Score back = through_file(score_of(staff), "lyrics");
        std::vector<std::string> sung;
        for (const auto& measure : back.parts[0].staves[0].measures)
            for (const auto& object : measure.objects)
                if (const auto* note = std::get_if<NotationNote>(&object)) sung.push_back(note->lyric);
        check((sung == std::vector<std::string>{"Al-", "le-", "lu-", "ia", "men", "_", "_"}),
              "lyrics: every syllable and hold comes back");
    }

    // Scenario: Parts, staves, transposition, a pickup and a change of meter
    {
        Score score;
        Part piano;
        piano.name = "Piano";
        Staff right;
        Staff left;
        left.clef = Clef(ClefKind::Bass);
        Measure pickup;
        pickup.pickup = true;
        pickup.objects = {pitched(Step::G, 0, 4, NoteValue::Quarter)};
        Measure pickup_low = pickup;
        pickup_low.objects = {pitched(Step::G, 0, 2, NoteValue::Quarter)};
        Measure bar;
        bar.objects = {pitched(Step::C, 0, 5, NoteValue::Whole)};
        Measure waltz;
        waltz.time = TimeSignature(3, 4);
        waltz.key = KeySignature(-1);
        waltz.objects = {pitched(Step::B, -1, 4, NoteValue::Half, 1)};
        right.measures = {pickup, bar, waltz};
        Measure bar_low = bar;
        bar_low.objects = {pitched(Step::C, 0, 3, NoteValue::Whole)};
        Measure waltz_low = waltz;
        waltz_low.objects = {pitched(Step::F, 0, 2, NoteValue::Half, 1)};
        left.measures = {pickup_low, bar_low, waltz_low};
        piano.staves = {right, left};
        score.parts.push_back(piano);
        Part clarinet;
        clarinet.name = "Clarinet in B-flat";
        Staff written;
        written.transpose_semitones = -2;
        Measure line;
        line.objects = {pitched(Step::D, 0, 5, NoteValue::Whole)};
        written.measures = {line};
        clarinet.staves = {written};
        score.parts.push_back(clarinet);

        const Score back = through_file(score, "parts");
        check(back.parts.size() == 2 && back.parts[0].staves.size() == 2, "parts: the piano is a grand staff");
        check(back.parts[0].staves[1].clef == Clef(ClefKind::Bass), "parts: the left hand reads bass clef");
        const auto& measures = back.parts[0].staves[0].measures;
        check(measures.size() == 3 && measures[0].pickup && !measures[1].pickup, "parts: the pickup stays a pickup");
        check(measures.size() == 3 && measures[2].time && *measures[2].time == TimeSignature(3, 4) &&
                  measures[2].key && measures[2].key->fifths() == -1,
              "parts: the waltz bar changes meter and key where it is written");
        const auto right_hand = rendered_notes(to_pattern(back.parts[0].staves[0], 480));
        check(right_hand.size() == 3 && std::get<0>(right_hand[1]) == 480 && std::get<0>(right_hand[2]) == 480 + 1920,
              "parts: the bar after the pickup starts a beat in");
        check(back.parts[1].staves[0].transpose_semitones == -2, "parts: the clarinet keeps its transposition");
        const auto clarinet_sound = rendered_notes(to_pattern(back.parts[1].staves[0], 480));
        check(clarinet_sound.size() == 1 && std::get<1>(clarinet_sound[0]) == 72, "parts: written D5 sounds C5");
    }

    // Scenario: Unmetered chant has no time signature and no bars
    {
        Staff chant;
        chant.time = TimeSignature::unmetered();
        Measure line;
        for (int i = 0; i < 5; ++i) {
            auto note = pitched(Step::D, 0, 4, NoteValue::Quarter);
            note.duration.stemless = true;
            line.objects.emplace_back(note);
        }
        chant.measures = {line};
        const std::string xml = write(score_of(chant));
        check(xml.find("<senza-misura") != std::string::npos && xml.find("<beats>") == std::string::npos,
              "chant: written senza misura");
        check(xml.find("<barline") == std::string::npos, "chant: no closing barline");
        const Score back = through_file(score_of(chant), "chant");
        const auto& staff = back.parts[0].staves[0];
        check(!staff.time.metered() && staff.measures.size() == 1, "chant: comes back unmetered, one line");
        const auto& first = std::get<NotationNote>(staff.measures[0].objects[0]);
        check(first.duration.stemless, "chant: noteheads stay stemless");
    }

    // Scenario: The written document keeps MusicXML's element order
    {
        Score score;
        Part piano;
        Staff right;
        Staff left;
        left.clef = Clef(ClefKind::Bass);
        left.transpose_octaves = -1;
        Measure bar;
        bar.objects = {pitched(Step::C, 0, 4, NoteValue::Whole)};
        right.measures = {bar};
        left.measures = {bar};
        piano.staves = {right, left};
        score.parts.push_back(piano);
        const std::string xml = write(score);
        const auto at = [&](const std::string& what, std::size_t from = 0) { return xml.find(what, from); };
        check(at("<key number=\"2\"") < at("<time number=\"1\""), "order: every key before any time");
        check(at("<time number=\"2\"") < at("<staves>") && at("<staves>") < at("<clef number=\"1\""),
              "order: times, then staves, then clefs");
        check(at("<clef number=\"2\"") < at("<transpose"), "order: clefs before transpose");
    }

    // Scenario: A file from another program imports at the right times
    {
        // MuseScore-style: two divisions to the quarter, a chord, a second
        // voice after <backup>, a <forward> gap, triplets, and a grand staff
        // whose one <key> has no number.
        const std::string xml = R"(<?xml version="1.0"?>
<score-partwise version="4.0"><part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list>
<part id="P1">
<measure number="1">
  <attributes><divisions>6</divisions><key><fifths>2</fifths></key><time><beats>4</beats><beat-type>4</beat-type></time>
    <staves>2</staves><clef number="1"><sign>G</sign><line>2</line></clef><clef number="2"><sign>F</sign><line>4</line></clef></attributes>
  <note><pitch><step>D</step><octave>4</octave></pitch><duration>6</duration><voice>1</voice><type>quarter</type><staff>1</staff></note>
  <note><chord/><pitch><step>F</step><alter>1</alter><octave>4</octave></pitch><duration>6</duration><voice>1</voice><type>quarter</type><staff>1</staff></note>
  <forward><duration>6</duration></forward>
  <note><pitch><step>A</step><octave>4</octave></pitch><duration>2</duration><voice>1</voice><type>eighth</type>
    <time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification><staff>1</staff></note>
  <note><pitch><step>B</step><octave>4</octave></pitch><duration>2</duration><voice>1</voice><type>eighth</type>
    <time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification><staff>1</staff></note>
  <note><pitch><step>C</step><alter>1</alter><octave>5</octave></pitch><duration>2</duration><voice>1</voice><type>eighth</type>
    <time-modification><actual-notes>3</actual-notes><normal-notes>2</normal-notes></time-modification><staff>1</staff></note>
  <note><pitch><step>D</step><octave>5</octave></pitch><duration>6</duration><voice>1</voice><type>quarter</type><staff>1</staff></note>
  <backup><duration>24</duration></backup>
  <note><pitch><step>D</step><octave>3</octave></pitch><duration>24</duration><voice>2</voice><type>whole</type><staff>2</staff></note>
</measure>
<measure number="2">
  <note><pitch><step>G</step><octave>4</octave></pitch><duration>9</duration><voice>1</voice><type>quarter</type><dot/><staff>1</staff></note>
  <note><grace/><pitch><step>A</step><octave>4</octave></pitch><voice>1</voice><type>eighth</type><staff>1</staff></note>
  <note><rest/><duration>15</duration><voice>1</voice><staff>1</staff></note>
</measure>
</part></score-partwise>)";
        const Score score = parse(xml);
        check(score.parts.size() == 1 && score.parts[0].staves.size() == 2, "foreign: a grand staff");
        check(score.parts[0].staves[1].key.fifths() == 2, "foreign: a <key> with no number holds on every staff");
        const auto treble = rendered_notes(to_pattern(score.parts[0].staves[0], 480));
        const std::vector<std::tuple<Tick, int>> expected = {
            {0, 62}, {0, 66}, {960, 69}, {1120, 71}, {1280, 73}, {1440, 74}, {1920, 67}};
        std::vector<std::tuple<Tick, int>> heard;
        for (const auto& note : treble) heard.emplace_back(std::get<0>(note), std::get<1>(note));
        check(heard == expected, "foreign: the chord, the gap, the triplets and the dotted note land where written");
        const auto bass = rendered_notes(to_pattern(score.parts[0].staves[1], 480));
        check(bass.size() == 1 && std::get<0>(bass[0]) == 0 && std::get<2>(bass[0]) == 1920,
              "foreign: the second staff's voice starts at the bar after <backup>");
    }

    // Scenario: A broken or hostile file is refused, never crashing or exhausting memory
    {
        const std::string quarter = "<note><pitch><step>C</step><octave>4</octave></pitch><duration>1</duration>"
                                    "<voice>1</voice><type>quarter</type>";
        check(throws<std::runtime_error>("<not-xml"), "refused: malformed XML");
        check(throws<std::runtime_error>("<score-timewise/>"), "refused: a timewise score");
        check(throws<std::runtime_error>(document("<divisions>1</divisions><staves>100000000</staves>", "")),
              "refused: a hundred million staves");
        check(throws<std::runtime_error>(document("<divisions>1</divisions><key number=\"0\"><fifths>1</fifths></key>", "")),
              "refused: staff number zero");
        check(throws<std::runtime_error>(document("<divisions>1</divisions><key number=\"-1\"><fifths>1</fifths></key>", "")),
              "refused: a negative staff number");
        check(throws<std::runtime_error>(document("<divisions>1</divisions>", quarter + "<staff>99999</staff></note>")),
              "refused: a note on staff 99999");
        check(throws<std::runtime_error>(document("<divisions>0</divisions>", "")), "refused: zero divisions");
        check(throws<std::runtime_error>(
                  document("<divisions>1</divisions><time><beats>3</beats><beat-type>3</beat-type></time>", "")),
              "refused: a beat type of three");
        check(throws<std::runtime_error>(document("<divisions>1</divisions>",
                                                  "<note><pitch><step>C</step><octave>4</octave></pitch>"
                                                  "<duration>-4</duration><voice>1</voice></note>")),
              "refused: a negative duration");
        check(throws<std::runtime_error>(document("<divisions>1</divisions>",
                                                  "<note><pitch><step>C</step><octave>4</octave></pitch>"
                                                  "<duration>1000000000000</duration><voice>1</voice></note>")),
              "refused: a note a trillion quarters long");
        check(throws<std::runtime_error>(document("<divisions>1</divisions>",
                                                  "<forward><duration>1000000000000</duration></forward>" + quarter + "</note>")),
              "refused: a forward a trillion quarters long");
        const Score additive = parse(document("<divisions>1</divisions><time><beats>3+2</beats><beat-type>8</beat-type></time>", ""));
        check(additive.parts[0].staves[0].time == TimeSignature(5, 8), "accepted: 3+2/8 is five eighths");
        const Score high = parse(document("<divisions>1</divisions>",
                                          "<note><pitch><step>C</step><octave>10</octave></pitch><duration>1</duration>"
                                          "<voice>1</voice><type>quarter</type></note>"));
        int dropped = 0;
        (void)to_pattern(high.parts[0].staves[0], 480, &dropped);
        check(dropped == 1, "accepted: a note above MIDI 127 parses, and is left out when played");
    }

    if (failures != 0) {
        std::cerr << failures << " MusicXML check(s) failed\n";
        return 1;
    }
    std::cout << "MusicXML scenarios passed\n";
    return 0;
}
