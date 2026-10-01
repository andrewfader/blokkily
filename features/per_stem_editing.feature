Feature: Each stem is edited and heard on its own, inside one pattern
  A producer builds a song from several stems, each a track that owns its
  instrument. A pattern (VERSE, CHORUS) holds a part for every stem, the way a
  channel rack does: selecting a stem shows that stem's part of the open
  pattern, notes added to one stem stay on it and are heard through its
  instrument, and a pattern placed in the arrangement plays on every stem.
  Each stem's lane can still leave a pattern out or play it on its own.

  Background:
    Given a song with two stems, each a CLAP instrument on its own track
    And one pattern, VERSE, placed at bar 1, so each stem plays its part of it there

  Scenario: Selecting a stem keeps the pattern open and shows that stem's part
    Given the first stem is selected and holds notes on steps 2, 6 and 10
    When the producer selects the second stem from the mixer
    Then VERSE is still the open pattern
    And the editors show the second stem's part of it, which is empty
    And the pattern's title names VERSE and the second stem

  Scenario: Notes added to a stem stay on that stem and are heard on its instrument
    Given only the first stem holds notes
    When the song plays from the start
    Then the first stem's track produces audio and the second stem's is silent
    When the producer draws notes on steps 4 and 8 on the second stem
    Then the second stem's part holds those two notes
    And the first stem's part still holds its three notes
    And both stems' tracks produce audio

  Scenario: The stem rack shows every stem's steps and edits any of them
    Given both stems hold notes in VERSE
    Then the stem rack shows one row per stem with each stem's own steps
    When the producer clicks step 12 on the first stem's row
    Then the first stem is selected and its part gains that step
    And the second stem's part is unchanged
    When the producer clicks the second stem's name in the rack
    Then the second stem is selected and no notes change

  Scenario: A new stem is written to straight away
    When a new stem is added from the arrangement
    Then it is selected with an empty part of VERSE, placed where VERSE plays
    When the producer draws a note on step 5 in the piano roll
    Then the note lands on the new stem's part and on no other stem's
    And the new stem's track produces audio
    And one undo takes the note back and keeps the stem's part

  Scenario: Pressing the lanes places, removes and opens the right stems
    Given a second pattern, CHORUS, is open and the first stem is selected
    When the producer presses an empty bar on the second stem's lane
    Then CHORUS is placed at that bar on every stem, and the second stem is selected
    When the producer presses that bar on the second stem's lane with the right button
    Then only the second stem's clip is taken away
    When the producer presses an empty bar on the second stem's lane with Ctrl held
    Then only the second stem plays its part of CHORUS there
    When the producer presses that filled bar
    Then CHORUS is opened on the second stem's part

  Scenario: The right button on a stem's name offers that stem's menu
    Given the second stem is selected
    When the producer presses the first stem's name in the arrangement with the right button
    Then the first stem is selected
    And the track menu opens for the first stem

  Scenario: Saved and opened again, every stem keeps its own part
    When the project is saved and opened again
    Then both patterns come back
    And every stem's part of VERSE holds the notes it held
    And a non-empty 1280 by 1080 screenshot is captured

  Scenario: The devices card names the selected stem's own instrument
    Given the new stem carries a different instrument from the second stem
    When the producer selects the new stem and then the second stem from the mixer
    Then the devices card names each stem's own instrument in turn

  Scenario: A double click on a stem's name asks to rename that stem
    Given the second stem is selected
    When the producer double-clicks the first stem's name in the arrangement
    Then the first stem is selected
    And the rename box opens for the first stem
