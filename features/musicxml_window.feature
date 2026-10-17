Feature: Scores come in and go out of the window as MusicXML
  A hymn, a choral piece or a concerto written in notation software comes in
  through SCORE IN, in the SCORE view's header, and becomes part of the song: a section of its own, a
  track per staff named after its part, with its notes, lyrics, clefs, key,
  meter and - for a song with nothing in it yet - its tempo. SCORE OUT writes
  the whole song as a score, and the written file is read back and compared
  with what the song plays. Lyrics are the song's: each note carries the
  syllable sung on it, in any script, typed in the SCORE view's LYRICS field.

  Proved by the bdd_score_file gate (src/app/verify/scenario_score_file.cpp)
  with tests/fixtures/shalom-aleichem.musicxml: a hymn of our own setting
  Shalom Aleichem's words, pointed Hebrew, for voice and organ in F major
  3/4. The gate saves build/artifacts/score-file.png.

  # bdd_score_file "score-file: SCORE IN builds the song from a hymn"
  Scenario: SCORE IN builds the song from a hymn
    Given a song with no notes, playing the CLAP fixture
    When the producer presses SCORE IN in the SCORE view, which opens its file dialog, and chooses the hymn
    Then the song has three tracks named Voice, Organ 1 and Organ 2 and a section named Shalom Aleichem
    And the section is in F major and 3/4, the song is in 3/4 at 76 BPM, and the transport says so
    And the voice holds eleven notes over four bars with their Hebrew syllables, the organ's right hand eight and its left hand four in the bass clef
    And every track plays the song's instrument, and the production render callback hears the hymn
    And one UNDO takes the whole import back, and REDO brings it again

  # bdd_score_file "score-file: the hymn's lyrics show under the staff, in Hebrew"
  Scenario: The hymn's lyrics show under the staff, in Hebrew
    Given the hymn has come in
    When the producer opens the SCORE view on the voice
    Then every syllable is drawn under its note in the bundled Hebrew face, its hyphen between it and the next
    And the score names F major in 3/4, and the LYRICS field shows the line

  # bdd_score_file "score-file: typing lyrics lays them on the notes"
  Scenario: Typing lyrics lays them on the notes
    When the producer types an English line in the LYRICS field
    Then its syllables land on the voice's notes in order, the rest of the line left over
    And one UNDO puts the Hebrew back

  # bdd_score_file "score-file: SCORE OUT writes the song and reads it back"
  Scenario: SCORE OUT writes the song and reads it back
    When the producer presses SCORE OUT, which opens its file dialog, and names a file
    Then the file is written, read back, and plays the song's notes exactly, as the status says
    And it holds three parts and the tempo
    And importing it again adds a second copy of the hymn, lyrics and all

  # bdd_score_file "score-file: a broken file is refused and changes nothing"
  Scenario: A broken file is refused and changes nothing
    When the producer imports a file declaring a hundred million staves, or a file that is not there
    Then the import is refused with a reason, and the song is as it was
