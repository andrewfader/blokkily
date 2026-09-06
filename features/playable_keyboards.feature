Feature: Play the song in the tuning it is written in
  Twelve equal semitones are one tuning among many. A session says which tuning
  and which scale it is in, every playable surface is laid out from that, and a
  note played on one of them keeps the pitch it was played at all the way to the
  instrument.

  Scenario: The keyboard is laid out in the song's tuning
    Given a session written in twelve-tone equal temperament
    Then the piano surface offers one key per degree of the tuning
    And each key is named the way that tuning names it
    When the producer chooses a nineteen-tone tuning
    Then the same register offers a key for every one of its degrees
    And each key reports the retune its instrument will be told

  Scenario: The scale marks the keyboard without silencing anything
    Given the song is in a major scale rooted on C
    Then the keys belonging to that scale are marked as belonging to it
    And the root of the scale is marked on every octave
    And the keys outside the scale can still be played

  Scenario: Auto-scale plays the note the song is in
    Given the song is in a major scale rooted on C with auto-scale on
    When the producer plays the key between the second and third degrees
    Then the nearest degree of the scale sounds instead
    When auto-scale is turned off
    Then that key sounds the pitch it is drawn at

  Scenario: A key played on the surface is written into the pattern
    Given a step of the canonical pattern is selected
    When the producer plays a key on the surface
    Then that step carries the pitch that was played
    And the tracker, the piano roll, and the step grid all name it
    And the note is heard through the selected track's instrument, stopped or
      playing

  Scenario: A microtonal note reaches the instrument as itself
    Given the song is in twenty-four-tone equal temperament
    When the producer plays the degree between two twelve-tone keys
    Then the pattern stores the nearer key and the retune away from it
    And the arrangement sounds that pitch rather than the key alone
    And reloading the session brings back the same pitch

  Scenario: Changing the surface does not change the music
    Given a pattern played in on the piano surface
    When the producer switches to the isomorphic grid, the fretboard, or the
      chord pads
    Then the pattern is untouched
    And every surface is laid out from the same tuning and scale

  Scenario: A chord pad plays a chord of the scale
    Given the chord pads are the surface in view
    Then each pad is named by the numeral of the chord it sounds
    And a row of pads is the same harmony one inversion further up
    When the producer plays a pad
    Then every voice of that chord sounds
    And the step carries one chord event rather than several notes
