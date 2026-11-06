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

  Scenario: An isomorphic grid is tiled as a honeycomb
    Given the isomorphic grid is the surface in view
    When the producer chooses a hexagonal layout
    Then alternate rows are offset by half a cell
    And the rows overlap the way interlocking hexagons do
    And every cell is drawn as a hexagon rather than a square
    And a press outside the drawn hexagon plays the neighbour it lands in

  Scenario: A layout drawn on a square grid stays square
    Given the isomorphic grid is the surface in view
    When the producer chooses a layout drawn on a square grid
    Then the cells sit on whole rows and columns
    And the intervals under the hands are unchanged

  Scenario: Any surface can be turned on its side
    Given a surface laid out across the window
    When the producer turns it to run down the window
    Then what ran to the right now runs upward
    And the surface asks for the room its turned layout needs
    And no note of the pattern and no pitch of a cell has changed

  # bdd_default "keyboards"
  Scenario: The surface marks the key played and the next two notes of the scale
    Given a piano surface in the song's scale
    When a key is played
    Then that key is ringed amber, the lowest in-scale key above it blue and breathing, and the in-scale key after that violet
    And each ring is on exactly one key, and past the top of the surface the suggestions wrap to the lowest in-scale keys

  # bdd_default "keyboards"
  Scenario: The surface pulses with the music
    Given the open pattern placed under the playhead
    When the song plays a step that holds a key
    Then that key is lit on the surface while it sounds
    And with BEAT FLASH on in SETTINGS (off as it ships) the surface flashes faintly on every beat

  # bdd_default "keyboards"; bdd_window_layout (the knobs)
  Scenario: A note effect bursts over every key played
    Given NOTE FX in SETTINGS set to FIRE
    When a key is played
    Then a burst of embers rises from that key and is gone once it has faded
    And LIGHTNING, WATER, TORNADO and STARMAN each burst in their own way, and NONE draws nothing
    And the INTENSITY, SIZE and DECAY knobs beside the picker set how many particles, how far and how long

  Scenario: A surface too large for its panel scrolls instead of shrinking
    Given a surface whose rows do not fit the keyboard panel
    Then its keys keep a size that can be hit
    And the surface can be scrolled to reach the rest of them
    And grid cells remain at least 40 pixels wide and high in either orientation
    And the rendered keys are clipped to their viewport without painting over the mixer
