Feature: The sequencer plays, mixes and saves what was written
  # Executable mapping: sequencer_fidelity_<case>, tests/sequencer_fidelity_tests.cpp.

  Scenario: A key struck again while it still sounds hands over to the new note
    Given a pattern whose step holds a key for four steps
    And the next step strikes the same key for four steps
    When the production render callback plays the pattern through the CLAP fixture
    Then the second note sounds until its own end
    And the first note is released where the second one starts
    # Executable: sequencer_fidelity_overlap_hands_over

  Scenario: A pattern placed again where it just ended plays its next loop
    Given a pattern with a step that plays every other loop
    When the pattern is placed four times in a row on one track
    Then the track plays what one clip of the pattern repeated four times plays
    And a placement after a gap is the pattern's first loop again
    # Executable: sequencer_fidelity_loop_numbers_continue

  Scenario: An inversion moves whole voices by octaves, below as above
    Given a major triad
    When it is scheduled at inversions 0, 1, -1 and 4
    Then inversion -1 drops the fifth an octave below the root
    And inversion 4 is the first inversion an octave up
    # Executable: sequencer_fidelity_inversion_octaves

  Scenario: An imported pitch struck again inside one step becomes a ratchet
    Given a MIDI file that strikes one pitch twice within a sixteenth
    When both hits are imported onto the pattern
    Then the step holds one note with two ratchets
    And a live take that re-strikes a pitch on its step still leaves the step alone
    # Executable: sequencer_fidelity_roll_becomes_ratchet

  Scenario: A release ends the most recent press of its key
    Given a key pressed twice before it is let go
    When the first release arrives
    Then it ends the later press and the earlier press keeps its own release
    # Executable: sequencer_fidelity_release_pairs_newest_press

  Scenario: An edit that lengthens the song leaves the playhead where it was
    Given a one-bar song played two and a half times through
    When the song is recompiled three bars long while it plays
    Then the playhead is still half a bar in
    # Executable: sequencer_fidelity_playhead_keeps_place

  Scenario: A fader pulled while the song plays ramps rather than steps
    Given a track sounding a held note at unity
    When the track is muted from the interface
    Then the next block falls to silence by at most a hundredth per sample
    And it begins where the last block ended
    # Executable: sequencer_fidelity_fader_move_ramps

  Scenario: A bounce's tail holds the end of the song, not its start
    Given a track with an echo and a gain lane rising from silence to unity
    When the arrangement is bounced with a tail
    Then the echo in the tail is heard at the lane's end value
    # Executable: sequencer_fidelity_bounce_tail_holds_end

  Scenario: A note played ahead of the first downbeat is exported
    Given a step with negative micro-timing on the first downbeat
    When the song is written as a MIDI file and read back
    Then the note starts on the downbeat and keeps the rest of its length
    # Executable: sequencer_fidelity_pickup_note_exported

  Scenario: A project's numbers read the same whatever the machine's decimal mark
    Given a session with a fractional tempo, gain and pan
    When it is serialized and loaded back
    Then every number is written with a point and reads back exactly
    And a comma decimal or a NaN in the file is refused
    # Executable: sequencer_fidelity_project_numbers_locale_free
