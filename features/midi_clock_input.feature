Feature: Following an external MIDI clock
  EXT in the SYNC row of SETTINGS follows the clock arriving at the MIDI input,
  as a drum machine or another sequencer sends it: Start plays the song from
  the top, Continue plays on, Stop stops, a Song Position Pointer moves the
  playhead, and the 24 ticks a beat set the tempo the song plays at - read
  as the middle half of a beat of tick intervals, so a jittery clock is
  steady and one late tick is no tempo change. The song's own tempo map is
  kept as it is and is back when EXT is off. The SYNC row shows the
  tempo heard. The song is held on the clock's beat, not only at its tempo:
  eight beats of ticks measure the tempo by least squares, the song's heard
  position is compared with each tick, and the tempo is trimmed (at most 2%)
  to close the difference; a song that starts a long way off is moved onto
  the beat once.

  # midi_clock_follower_tempo
  Scenario: The ticks say the tempo
    Then half a beat of ticks says nothing, a steady beat at 120 says 120
    And a clock jittered by 0.4 ms at 97.5 says 97.5 within half a BPM
    And one late tick changes nothing, two silent seconds say nothing, and a pause starts afresh

  # midi_clock_clock_lock
  Scenario: The lock measures the clock and holds the song on its beat
    Given a clock at 120.037 BPM jittered by 0.4 ms, and a song that starts 20 ms late
    Then eight beats of ticks say the tempo within a hundredth of a BPM
    And the song is on the clock's beat within 0.6 ms for ten minutes, where at 120.0 it would drift 185 ms

  # midi_clock_follower_transport
  Scenario: Start, Stop, Continue and the pointer
    Then a pointer seeks in sixteenths, Start runs from the top, Stop stops and Continue runs on

  # midi_clock_input_feed
  Scenario: The input hands the clock to the control thread
    When Start, a tick, a note, a pointer, Continue and Stop arrive
    Then the five clock messages come out in order, stamped as they arrived
    And the note still reaches the instrument

  # midi_clock_engine_override
  Scenario: The song plays at the clock's tempo
    Given a CLAP note on beat two of a song at 120
    When a clock at 60 is followed
    Then the note sounds at sample 48000 rather than 24000 and the song lasts twice as long
    And the song's own tempo map is not touched

  # bdd_midi_clock
  Scenario: EXT in the window
    Given the CLAP synth on track 0 and a MIDI input
    When EXT is on and a clock at 100 BPM sends Start and ticks
    Then the song plays, the SYNC row says 100.0 BPM in full, and the song is placed at 100
    And its instruments are not rebuilt, and its tempo map and saved state are unchanged
    When the clock sends Stop and a pointer to bar 3
    Then the song stops and the playhead is at bar 3
    When EXT is off the song plays at its own tempo again

  # bdd_midi_clock_lock
  Scenario: Two minutes on the clock's beat in the window
    Given a one-bar song with a note on every beat, following a clock at 100.0049 BPM jittered by 0.4 ms
    When it plays for two minutes of song
    Then every note in the last minute, found in the rendered output, is within a millisecond of the clock's beat
    And SYNC reads 100.0 BPM
