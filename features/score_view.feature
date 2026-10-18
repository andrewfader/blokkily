Feature: The pattern is also shown as true staff notation
  A SCORE view takes the editors' place: the open part drawn as a staff with
  its clef, key and time signature, notes where they fall in time, rests,
  barlines, ties, chords with one stem, voices with their stems apart,
  accidentals by the bar's rule and lyrics, in the Bravura SMuFL font. It
  draws the one pattern, and notes entered on its staff are written into
  that pattern, so it cannot disagree with the other editors. The clef, key and CHANT controls set the song's
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
    Given an eight-bar part with a triad, then a C3 held through a bar under four C6 quarters
    When the score draws it
    Then there are eight bars, the triad has one stem, and the two voices' stems point apart
    And the staff fits its highest and lowest notes, and scrolls sideways to the end of the part

  # bdd_score "score: a tuplet is bracketed over the notes it counts"
  Scenario: A tuplet is bracketed over the notes it counts
    Given two groups of eighth-note triplets, the second climbing above the staff
    When the part is drawn
    Then each group's first note carries a 3 and a bracket over its three notes
    And each bracket clears every head and stem under it, above the staff

  # bdd_score "score: eighths and shorter are beamed by the beat"
  Scenario: Eighths and shorter are beamed by the beat
    Given two eighths, four sixteenths, an eighth and two sixteenths, and a quarter, a beat each
    When the part is drawn
    Then each of the first three beats is one beam, the sixteenths joined by a second beam
    And every beamed note draws no flag, its stem pointing the group's way and ending on its beam
    And the quarter keeps its own stem

  # bdd_score "score: notes are written and edited on the staff"
  Scenario: Notes are written and edited on the staff
    Given the SCORE view shows an empty bar and the quarter note is chosen
    When the producer clicks the treble staff's second line on beat two
    Then a quarter-note G4 is written into the pattern there, selected, and one UNDO takes it back
    When the producer presses Up, then Shift+Up, then the sharp chip
    Then the note is A4, then A5, then A#5, drawn with its sharp
    When the producer clicks below it at the same time
    Then a C4 is written beside it as a chord tone, and Delete takes only the C4
    When the producer picks a dotted half and clicks the middle line on the downbeat
    Then a dotted-half B4 is written and drawn with its dot
    When the key is D major and the producer clicks the top line
    Then the note written is F#5, from the key signature
    And the piano roll's lookups find every note the staff wrote
