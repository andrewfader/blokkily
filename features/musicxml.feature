Feature: MusicXML import and export
  As a composer working in Blokkily
  I want scores to move between Blokkily and notation software
  So that a concerto or hymn written here opens correctly elsewhere, and back

  Proved by blokkily_musicxml_tests (tests/musicxml_tests.cpp). Export follows
  the repo's export rule: the document is written to a file, the file is read
  back, and what the scheduler plays from it is compared with the pattern
  that was exported.

  Scenario: An exported pattern plays back the same from the file
    Given four bars with a dotted note, a triad, a note tied over a barline, a
      note held two bars under moving notes, triplets with retunes and a gap
    When it is exported as notation to a MusicXML file and the file is read back
    Then the scheduler plays every note at the same time, length, key, retune
      and velocity, and the pattern is four bars long

  Scenario: Exporting every clef in the roster
    Given a score with one staff per clef, French violin to percussion
    When it is exported and read back from the file
    Then every clef, the title and the composer are unchanged

  Scenario: Key signatures from seven flats to seven sharps
    Given staves with key signatures of -7 through +7 fifths
    When each is exported and read back
    Then every key signature is unchanged

  Scenario: Lyrics keep their words and their held syllables
    Given "Al-le-lu-ia" across four notes and "men" held over two more
    When the score is exported
    Then the word is written begin, middle, end and the hold as an extend line
    And reading it back gives every syllable and hold again

  Scenario: Parts, staves, transposition, a pickup and a change of meter
    Given a piano grand staff starting with a pickup and changing to 3/4 and
      one flat in its third bar, and a B-flat clarinet part
    When the score is exported and read back
    Then the grand staff, the pickup, the change of meter and key where it was
      written, and the clarinet's transposition are kept
    And the bar after the pickup starts one beat in, and written D5 sounds C5

  Scenario: Unmetered chant has no time signature and no bars
    Given a stemless chant line with no meter
    When it is exported
    Then it is written senza misura with no closing barline
    And it reads back unmetered, one line, with stemless noteheads

  Scenario: The written document keeps MusicXML's element order
    Given a transposed grand staff
    When it is exported
    Then every key comes before any time, times before staves, staves before
      clefs, and clefs before transpose

  Scenario: A file from another program imports at the right times
    Given a document at six divisions per quarter with a chord, a <forward>
      gap, eighth-note triplets, a dotted note, a grace note and a second staff
      whose voice follows a <backup>, under one <key> with no number
    When it is read
    Then the key holds on both staves and every note lands where it was written

  Scenario: A broken or hostile file is refused, never crashing or exhausting memory
    Given malformed XML, a timewise score, a hundred million staves, staff
      numbers of 0, -1 and 99999, zero divisions, a beat type of three, a
      negative duration, and a note or a forward a trillion quarters long
    When each is read
    Then each is refused with an error
    And an additive 3+2/8 meter reads as five eighths
    And a note above MIDI 127 reads, and is left out when played
