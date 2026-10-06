Feature: A sequencer can edit a complete song and exchange its notes
  # Executable mapping: bdd_sequencer_workflow, scenario_workflow.cpp.
  Scenario: Read and navigate simultaneous tracker channels
    Given two instrument tracks with different notes in one section
    When I click a tracker channel header and press Tab then Shift+Tab
    Then both channels have usable columns and the selected track follows the keyboard

  Scenario: Reach the entire piano roll pitch range
    Given an open pattern and an idle instrument engine
    When I use the piano roll pitch buttons to reach keys 0 and 127
    Then both pitches can be written into the canonical pattern
    And scrolling alone neither creates undo history nor rebuilds the engine

  Scenario: Edit a long arrangement without squeezing bars away
    Given a song with clips at bars 1 and 64
    When I zoom in and scroll to the final bar
    Then its clip is in the viewport and at least 40 pixels wide

  Scenario: Import MIDI as one reversible edit
    Given an arrangement exported through MIDI OUT
    When I import the file through the application controller
    Then new sections follow the existing arrangement
    And undo restores the previous arrangement in one step
    And a failed import leaves no undo entry

  # Executable mapping: midi_file, tests/midi_file_tests.cpp; also GUI gate above.
  Scenario: Export the scheduled arrangement
    Given named instrument tracks placed repeatedly in the arrangement
    When I write a MIDI file and read the written file back
    Then its notes match the production scheduler's pitch, start, duration and velocity
    And drum notes use MIDI channel 10
    And tempo ramps, meter changes and pitch bend survive export

  Scenario: Read running status and reject truncated MIDI
    Given a format 0 MIDI file with running status and velocity-zero note releases
    When I decode the file
    Then the note has the expected duration
    And missing track chunks, truncated chunks and invalid data bytes are rejected

  # Executable mapping: midi_file, tests/midi_file_tests.cpp.
  Scenario: Import multiple channels and late controllers
    Given a single MIDI track containing melodic and drum channels
    And a controller movement after the last note
    When I import the file
    Then the channels become separate instrument tracks
    And the late controller retains its position

  Scenario: Edit individual piano roll notes without replacing their neighbours
    Given two pitches drawn at the same step in the piano roll
    When I erase empty space or erase, resize, or move one voice
    Then the other voice keeps its pitch and duration
    And moving onto an occupied step preserves its other pitches
    And moving onto an identical pitch leaves both notes in place
    And grabbing the middle of a long note preserves the grab offset
    And a click without dragging only selects the note
    And one undo restores the whole gesture
    And both simultaneous pitches reach the production scheduler

  Scenario: Playback follows the whole pattern
    Given a two-bar pattern placed at bar two with a note on step 20
    And the edit cursor is on step 0
    When I press Play and the production audio callback renders step 20
    Then the tracker highlights step 20 and scrolls it into view
    And the piano roll playhead crosses column 20 rather than column 4
    And the edit cursor stays on step 0
    # Executable: bdd_sequencer_workflow, workflow: playback follows the whole pattern

  Scenario: Playback repeats and gaps follow the arrangement
    Given the open two-bar pattern repeats twice starting at bar two
    When playback enters step 6 of the second repeat
    Then the tracker highlights step 6
    When playback is moved before the clip
    Then neither editor shows the open pattern as playing
    When I press Play again
    Then playback stops
    # Executable: bdd_sequencer_workflow, workflow: playback repeats and gaps follow the arrangement
