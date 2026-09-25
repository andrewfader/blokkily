Feature: A MIDI keyboard plays the song and records into it
  A producer plugs in a keyboard and plays. What they play is heard at once
  through the selected track's instrument, whether or not the song is running,
  and with recording armed a running song writes what was played into the
  pattern under the playhead, where every editor shows it and the arrangement
  plays it back. Keys arrive on the MIDI port's own thread and reach the render
  callback through a lock-free queue, never waiting on the interface.

  The interface scenarios are exercised by the bdd_midi_recording gate
  (`blokkily --verify --scenario midi`) in src/app/gui_main.cpp, which names the
  scenario it failed in ("BDD FAIL at: midi: …"). The engine and take
  scenarios are also proved by blokkily_core in tests/core_tests.cpp, and the
  real MIDI layer by midi_device_input in tests/midi_device_check.cpp.

  Scenario: A port is chosen from the rendered panel
    Given the MIDI IN panel says "No MIDI input"
    When the producer clicks it
    Then a menu lists every input the system offers
    When they click one
    Then the panel names that port and the audio device is running

  Scenario: A key is heard on a stopped song
    Given the song is stopped
    When a key goes down on the keyboard
    Then the selected track's instrument sounds through the render callback
    And it keeps sounding while the key is held and stops when it is let go
    And the panel shows the note that arrived and its velocity
    And nothing is written into the pattern

  Scenario: The selected track is the one that sounds
    When the producer selects the second track and plays a key
    Then only the second track sounds
    When they select the first track before letting go
    Then the key is still released on the second track

  Scenario: A key reaches the application through the system's MIDI layer
    Given a virtual keyboard port offered by the MIDI server
    When the input opens it by name and a key is sent to it
    Then the key is heard through the production render callback
    And with the song playing and armed it is captured with its position

  Scenario: Recording is armed from the transport
    When the producer clicks the record button
    Then it shows armed

  Scenario: A take is written where it was played and heard back
    Given recording is armed and a step of the open pattern is empty
    When the song plays and a key is held across part of that step
    Then while the song is still running the step holds that key
    And its micro-timing is how far from the step the key went down
    And its length is how long the key was held
    And the step grid, the tracker and the piano roll all show it
    When the song is played over that step again with no key down
    Then the track sounds there, where before the take it was silent
    And one undo takes the whole take back and redo puts it back

  Scenario: Stopping ends the take
    Given a key is held while the song records
    When the producer stops the song
    Then the note is written as released where the song stopped
    And letting go of the key afterwards writes nothing more

  Scenario: An unarmed song records nothing
    Given recording is not armed
    When the song plays and a key is played
    Then it is heard and the pattern is unchanged

  Scenario: The keyboard plays in the song's tuning
    Given the song is in 19-EDO
    When a key is recorded
    Then the step holds the degree of 19-EDO that key stands for, with its
      retune, and the tracker names it the way the tuning does

  Scenario: Several keys on one step make a chord
    Given a step that sounds one pitch
    When a take plays another pitch on that step
    Then the step becomes a chord of both, keeping its locks and timing
    And each voice of the chord keeps the velocity it was played at
    And each voice keeps how long it was held, and the step lasts as long as its longest voice
    And a pitch the step already sounds is not written twice
    # Velocity and length per voice: features/chord_velocity.feature,
    # chord_velocity_write_played and the bdd_chord_velocity gate.

  Scenario: A note held over the loop point keeps its length
    When a key goes down before the end of the song and comes up after it loops
    Then the note lasts as long as it was held

  Scenario: A bounce is the arrangement, not the keyboard
    Given recording is armed and a key is played as a bounce starts
    Then the bounced file is the arrangement alone
    And nothing is recorded from it
    And the key is heard once the bounce hands the engine back

  Scenario: A take played after a tempo change lands where it was heard
    Given the song slows to 60 BPM from bar 2 and recording is armed
    When a key is played into bar 2 of the running song
    Then it is written on the step it was played in, with the micro-timing heard
    And the transport reads 60.00 BPM and bar 2 there
    # bdd_midi_recording "midi: a take across a tempo change lands where it
    # was played"; core proof in features/timebase.feature.
