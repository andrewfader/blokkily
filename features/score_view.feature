Feature: The pattern is also shown as true staff notation
  A SCORE view takes the editors' place: the open part drawn as a staff with
  its clef, key and time signature, notes where they fall in time, rests,
  barlines, ties, chords with one stem, voices with their stems apart,
  accidentals by the bar's rule and lyrics, in the Bravura SMuFL font. It
  draws the one pattern and never writes notes back, so it cannot disagree
  with the other editors. The clef, key and CHANT controls set the song's
  notation for the part: saved with the song and undone with it. The clef
  picker covers the full roster including the obscure clefs (French violin,
  subbass, the C clefs); CHANT draws an unmetered chant staff with bare
  noteheads for hymn and nusach-style melodies. A long part scrolls sideways
  and the staff grows to fit high and low notes.

  Proved by the bdd_score gate (src/app/verify/scenario_score.cpp), which
  saves build/artifacts/score-view.png and one screenshot per scenario.

  # bdd_score "score: the SCORE view draws the pattern's notes as notation"
  Scenario: The pattern's notes appear as notation
    Given the window at its canonical size in the SCORE view
    When the producer writes C4 E4 G4 on steps 0 4 8
    Then the score is on screen at a usable size, drawn in the SMuFL font
    And C4 hangs below the treble staff on a ledger line, E4 and G4 sit on its lines
    And they are sixteenths, left to right, with rests filling the bar to its closing barline
    And a note the step grid wrote shorter than its step is drawn to the end of that step

  # bdd_score "score: the clef picker reaches the obscure clefs"
  Scenario: Obscure clefs are one picker away, and the song keeps them
    Given the SCORE view is showing
    When the producer picks the French violin clef from the clef picker's eleven
    Then the song's part is written in French violin, and every note sits two steps lower
    And the clef comes back when the song is saved and opened again
    And a clef picked afterwards is taken back by UNDO

  # bdd_score "score: CHANT draws an unmetered staff"
  Scenario: Chant melodies get an unmetered staff
    Given the SCORE view is showing
    When the producer clicks CHANT
    Then the part is unmetered, with no barlines and bare noteheads without stems
    When the producer clicks CHANT again
    Then the staff is back in 4/4

  # bdd_score "score: the key picker re-spells accidentals"
  Scenario: Key signatures re-spell the staff
    Given the SCORE view shows the notes and an F4
    When the producer picks three sharps from the key picker
    Then the key signature's sharps sit on F, C and G of the treble staff and the score names A major
    And the F4 is drawn with a natural sign while the notes the key covers draw none

  # bdd_score "score: chords, held notes and long parts are drawn whole"
  Scenario: Chords, held notes and long parts are drawn whole
    Given a four-bar part with a triad, then a C3 held through a bar under four C6 quarters
    When the score draws it
    Then there are four bars, the triad has one stem, and the two voices' stems point apart
    And the staff fits its highest and lowest notes, and scrolls sideways to the end of the part
