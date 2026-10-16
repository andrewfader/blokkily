Feature: MusicXML import and export
  As a composer working in Blokkily
  I want scores to move between Blokkily and notation software
  So that a concerto or hymn written here opens correctly elsewhere, and back

  Scenario: Exporting every clef in the roster
    Given a score with one staff per clef (French violin, treble, subbass,
      bass, baritone F, soprano, mezzo-soprano, alto, tenor, baritone C,
      percussion)
    When the score is exported to MusicXML
    Then each staff carries its MusicXML sign and line
    And the percussion staff uses unpitched notes
    When the document is parsed back
    Then every clef, the title, the composer and the part name are unchanged

  Scenario: Key signatures from seven flats to seven sharps
    Given staves with key signatures of -7 through +7 fifths
    When the score is exported and parsed back
    Then every key signature is unchanged

  Scenario: Accidentals, dots, ties, voices and stemless notes
    Given notes with double sharps and flats, dotted values up to a
      triple-dotted 128th, a tie chain across two voices and a stemless note
    When the score is exported and parsed back
    Then every accidental, dot count, tie flag, voice and the stemless flag
      are unchanged
    And the re-imported score renders the same scheduled notes as the original

  Scenario: Lyrics, melisma and extender lines
    Given syllables with a melisma ("A", "-", "-") and an extender ("men", "_")
    When the score is exported and parsed back
    Then the syllabic begin/middle/end chain and the extender line round-trip

  Scenario: Parts, grand staff, measures, transposition, unmetered chant
    Given a piano part with treble and bass staves over two measures and a
      chant part with an unmetered alto staff transposed down an octave
    When the score is exported and parsed back
    Then the part list, the grand staff (via staves and backup), the measure
      count, the senza-misura time signature and the octave transposition
      are unchanged

  Scenario: Foreign files and malformed input
    Given a MusicXML document with unknown elements (directions, harmony)
    When it is parsed
    Then the unknown elements are skipped without failing
    Given malformed XML or a timewise document
    When it is parsed
    Then a std::runtime_error is thrown
