Feature: Write music in whichever editor is open
  A producer should be able to enter notes in the tracker or in the piano roll,
  not only in the step grid, and hear the result without leaving the editor.
  Every editor writes the one canonical pattern, so a note entered in any of
  them appears in all of them and in what the song plays.

  Scenario: The piano roll writes the lane that was pressed
    Given Blokkily is running with its demonstration pattern
    When the producer presses a lane of the piano roll over column 4
    Then the canonical pattern carries a note on step 4 at that lane's pitch
    And the step grid lights step 4
    And the tracker names that pitch on row 03
    And the piano roll draws the note on that lane

  Scenario: The piano roll erases with the right button
    Given a note the producer just drew on step 4
    When the producer right-clicks the same lane
    Then the step is empty in the canonical pattern
    And the step grid, the tracker, and the roll all show it empty

  Scenario: The piano roll draws a phrase by dragging
    Given the pointer is held down in the piano roll
    When it moves across columns and lanes
    Then each column it crosses takes the pitch of the lane it was crossed on

  Scenario: The tracker's note column takes a click
    Given the tracker's entry octave reads O3
    When the producer clicks the note column of row 05
    Then the canonical pattern carries C3 on step 5
    And the step grid and the piano roll show that note

  Scenario: The tracker is typed the way a tracker is typed
    Given the cursor is on row 05
    When the producer types X
    Then row 05 holds D3, because X is D in the tracker note layout
    And the cursor moves on to row 06
    When the producer presses Backspace on row 05
    Then row 05 is empty rather than holding a note again

  Scenario: A note entered in an editor is heard as it is entered
    Given a track carrying an instrument
    When a note is written in the tracker or the piano roll
    Then it is auditioned through the running engine
    And it sounds on a stopped transport as well as a playing one

  Scenario: An edit while the song plays is heard without restarting it
    Given an arrangement playing at 120 BPM and 48000 Hz
    And the playhead has passed sample 24000
    When the producer writes a note at tick 960
    Then the playhead is still where it was, not back at the start
    And nothing sounds until sample 48000
    And the note the producer just wrote sounds there
    When the producer erases that step while it is sounding
    Then the note is released rather than left ringing
    And the bar is silent up to the step that replaced it

  Scenario: An edit does not rebuild the audio graph
    Given a song prepared with an instrument on each track
    When the arrangement is recompiled for an edit
    Then the instruments are the same instances, still holding their state
    And a song with a different track list is refused so the caller rebuilds

  Scenario: The session opens with something to hear
    Given a machine with a General MIDI SoundFont installed
    When Blokkily starts
    Then every track without an instrument is given that bank
    And a track named DRUMS is given the percussion bank rather than a piano
    And Play sounds the demonstration song without a plugin hunt

  Scenario: A session that already has instruments is left alone
    Given a project whose tracks already carry instruments
    When the default instrument is offered
    Then nothing is replaced
