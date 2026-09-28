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

  Scenario: The piano roll lengthens a note by its right edge
    Given a note drawn on step 4
    When the producer drags that note's right edge across later columns
    Then the canonical pattern holds a longer duration on that step
    And the piano roll draws a wider note
    And the scheduler sounds it for that many ticks
    And the tracker still names the note on the row it started on

  Scenario: The piano roll moves a note by dragging it
    Given a note on step 4
    When the producer drags the body of that note to another lane and column
    Then the original step is empty
    And the destination holds that pitch
    And the step grid, the tracker and the roll all follow the move

  Scenario: The tracker's velocity column is an input
    Given a note on a tracker row
    When the producer drags the VEL column
    Then that step's velocity changes
    And the inspector and the step grid report the same value

  Scenario: A step can be copied and pasted
    Given a step that holds a note, a velocity and a lock
    When the producer copies it and pastes onto an empty row
    Then the destination holds the same pitch, length, velocity and lock
    And the original step is unchanged

  Scenario: A step is duplicated onto the next row
    Given a note on row 05
    When the producer presses Ctrl+D
    Then row 06 holds the same note
    And the cursor is on row 06

  Scenario: Insert pushes later rows down
    Given a note on row 05 and another on row 06
    When the producer presses Insert on row 05
    Then row 05 is empty
    And the note that was on row 05 is now on row 06
    And the note that was on row 06 is now on row 07

  Scenario: Shift+Backspace pulls later rows up
    Given a note on row 06 and another on row 07
    When the producer presses Shift+Backspace on row 06
    Then row 06 holds what was on row 07
    And row 07 is empty

  Scenario: The tracker's FX column writes a lock
    Given a note on a tracker row
    When the producer drags the FX column
    Then that step carries a parameter lock
    And the inspector reports the same lock value
    When they right-click the FX column
    Then the lock is cleared

  Scenario: Alt nudges micro-timing and note length
    Given a note on the selected step
    When the producer presses Alt+Right
    Then that step's micro-offset advances by one tick
    When they press Alt+Up
    Then that step is one step longer

  Scenario: Ctrl with a digit toggles that step
    Given the pattern is empty at step 3
    When the producer presses Ctrl+4
    Then step 3 holds a note in the entry octave
    When they press Ctrl+4 again
    Then step 3 is empty

  Scenario: Ctrl-click on the step grid seeks the song
    Given the song's playhead is on bar 1
    When the producer Ctrl-clicks step 6 of the rendered grid
    Then the audio engine's playhead is on step 6 of bar 1
    And every editor draws the playhead there

  Scenario: The piano roll keyboard sounds the lane that was pressed
    Given a track carrying an instrument
    When the producer presses a key of the roll's gutter
    Then that pitch is auditioned through the running engine
    And it is released when the key is let go

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

  Scenario: The window opens inside the screen it is shown on
    Given a screen whose free area is smaller than the 1280 by 1080 layout
    When Blokkily starts
    Then the window opens no larger than the screen's free area
    And the editor column scrolls to whatever does not fit
    Checked by the default gate at "the window opens inside the screen".

  Scenario: The inspector remains reachable in the combined view
    Given the window is 1280 by 1080 with all editors visible
    Then the complete step inspector fits inside the visible window
    And its controls retain a usable size
