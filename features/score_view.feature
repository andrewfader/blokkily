Feature: The pattern is also shown as true staff notation
  A SCORE view sits beside the step grid, the tracker and the piano roll: the
  same canonical pattern drawn as a staff with clefs, key and time signatures,
  note values, accidentals, ties and lyrics, rendered with the Bravura SMuFL
  font. It is a fourth projection of the one pattern — read-only for v1, so
  it can never disagree with the other editors. The clef picker covers the
  full roster including the obscure clefs (French violin, subbass, the C
  clefs); a CHANT switch draws an unmetered chant staff with bare noteheads
  for hymn and nusach-style melodies.

  # bdd_score "score: the SCORE view draws the pattern's notes as notation"
  Scenario: The pattern's notes appear as notation
    Given the window at its canonical size
    When the producer picks the SCORE view
    And the producer writes C4 E4 G4 on steps 0 4 8
    Then the score canvas is on screen at a usable size
    And the score shows three notes on the treble staff
    And a screenshot shows staff lines, a treble clef and three noteheads

  # bdd_score "score: the clef picker reaches the obscure clefs"
  Scenario: Obscure clefs are one picker away
    Given the SCORE view is showing
    When the producer picks the French violin clef
    Then the score names the French violin clef
    And the notes are re-spelled under the new clef

  # bdd_score "score: CHANT draws an unmetered staff"
  Scenario: Chant melodies get an unmetered staff
    Given the SCORE view is showing
    When the producer switches on CHANT
    Then no time signature is drawn
    And the notes are drawn as bare noteheads without stems

  # bdd_score "score: the key picker re-spells accidentals"
  Scenario: Key signatures re-spell the staff
    Given the SCORE view is showing
    When the producer picks a key with three sharps
    Then the key signature shows three sharps on the staff
    And the score names the key A major
