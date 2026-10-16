Feature: True staff notation as a fourth projection of the pattern
  Blokkily's editors — tracker, piano roll, step grid — are projections of one
  canonical pattern. Staff notation joins them: a score view that shows real
  staves with real clefs, key and time signatures, spelled note values,
  accidentals, ties and lyrics, so concertos, hymns and choral music can be
  written in Blokkily or by an agent driving it. The model scenarios below are
  proved by blokkily_notation_tests (tests/notation_tests.cpp), one case per
  scenario; the score-view scenarios are task C's and are proved by the
  screenshot gate, which renders the view and saves build/artifacts/notation.png
  for inspection.

  Scenario: Pitches convert between spelling and MIDI both ways
    Given middle C written as C4
    When it converts to MIDI and back
    Then the key is 60 and the spelling round-trips for all 128 keys
    And B#3 also converts to 60 while Cb4 converts to 59

  Scenario: Every clef names itself and places the staff's bottom line
    Given the eleven clefs from French violin to percussion
    When each names itself and reports its bottom line's diatonic position
    Then the names are the conventional English ones
    And treble sits on E4, French violin on G4, soprano on C4, mezzo-soprano on A3
    And alto on F3, tenor on D3, both baritones on B2, bass on G2, subbass on E2
    And only percussion reports itself unpitched

  Scenario: Key signatures alter the steps they claim, no others
    Given D major and Bb major
    When each is asked what accidental it imposes on every step
    Then D major sharpens F and C and nothing else
    And Bb major flats B and E and nothing else
    And 8 sharps or 8 flats are rejected
    And the major and minor tonics spell themselves correctly, even F# major and Eb major

  Scenario: Enharmonic spelling follows the key signature
    Given D major, F major, C major, C# major and Cb major
    When MIDI keys are spelled under each
    Then F#4 in D major is never written Gb, and Bb3 in F major is never A#4
    And C major breaks chromatic ties toward the sharps
    And C# major spells MIDI 60 as B#3 while Cb major spells it Cb4

  Scenario: Note values convert to ticks and back; odd lengths tie
    Given 480 ticks per beat
    When exact durations convert
    Then a quarter is 480 ticks, a dotted half is 1440, a breve is 3840
    And 720 ticks is a single dotted half while 500 ticks is not exact
    And 2880 ticks ties a whole to a half
    And 500 ticks rounds to the 128th-note grid and ties quarter plus 128th

  Scenario: A spelled pitch lands on the right line or space of its clef
    Given treble and bass clefs
    When spelled pitches are positioned
    Then E4 is treble's bottom line and middle C hangs two steps below it
    And G2 is bass's bottom line
    And an accidental prints exactly when the key signature does not already say it
    And ledger lines count correctly above and below the staff
    And 4/4 is metered while chant is unmetered with no barlines

  Scenario: A pattern renders to spelled, tied notation and replays back
    Given a pattern with a dotted-half D4 and a C major triad
    When it renders to a staff in D major under treble clef
    Then the melody is one dotted-half D4 and the triad is three voiced notes
    And a 2880-tick note renders as two notes chained by ties
    And replaying the staff reproduces every voice at its tick, key and length
    And a treble-8vb staff sounds an octave below its writing
    And rests advance the cursor while an unmetered chant staff packs notes end to end

  Scenario: Parts group staves under a titled score
    Given a score titled "Test Concerto"
    When a piano part holds treble and bass staves
    Then the part is a grand staff under the score's title and composer

  Scenario: The score view shows the pattern as engraved notation
    Given a pattern with notes in several clefs, keys and meters
    When the score view renders it
    Then staves show clefs, key signatures, time signatures, note values,
      accidentals, ties and lyrics, and the screenshot shows it

  Scenario: Editing the score edits the one pattern
    Given the score view showing a pattern's notation
    When a note is added, moved or deleted in the score view
    Then the tracker, piano roll and step grid show the same change
    And the change survives a project save and reload
