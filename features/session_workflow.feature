Feature: A session can be worked on without fear of losing it
  A workstation is used for hours at a time. Every edit can be taken back, the
  file a song came from is where it is saved, the patterns and tracks can be
  named, copied and removed, and the controls behave the same way whichever
  panel the pointer or the keyboard happens to be in.

  Each scenario is exercised by the "session workflow" group of the
  bdd_edit_once_see_everywhere gate in src/app/gui_main.cpp, which names the
  scenario it failed in ("BDD FAIL at: workflow: …").

  Scenario: An edit can be undone and redone
    Given a pattern with no steps
    When the producer clicks step 15 of the rendered step grid
    Then the step holds a note in the octave the tracker types in
    When they press Ctrl+Z
    Then the step is empty in every editor
    When they press Ctrl+Shift+Z
    Then the note is back

  Scenario: A stroke across the piano roll is one step of history
    Given a pattern with no steps
    When the producer drags across four columns of the piano roll
    Then four steps hold notes
    When they undo once
    Then all four are gone

  Scenario: The piano roll shows where the song is past its first bar
    When the song's playhead is on the fifth step of bar two
    Then the roll's playhead is drawn over its fifth column

  Scenario: The piano roll draws every voice of a chord
    When a chord pad writes a three-voice chord onto a step
    Then the roll draws three notes in that column

  Scenario: A pattern is copied, named, emptied and removed
    Given a pattern with four steps
    When the producer duplicates it
    Then a new pattern named after the original holds the same four steps
    When they rename it "drop"
    Then it is called DROP in the arrangement and above the editors
    When they clear it
    Then it holds no steps
    When they delete it
    Then the song no longer lists it

  Scenario: Deleting a pattern deletes its clips and nothing else
    Given a clip of the open pattern on the arrangement
    When the producer deletes the pattern
    Then its clip is gone and every other clip still names a pattern that exists
    When they undo
    Then the pattern and its clip are back

  Scenario: A track added from the interface can be heard at once
    When the producer presses +TRK
    Then the new track is selected and carries the SoundFont the session uses
    And a key played on it is rendered as non-silent audio on that track

  Scenario: A track is renamed from the keyboard
    When the producer asks to rename a track and types "keys two" and Return
    Then the track is called KEYS TWO and the rename field has closed

  Scenario: A track's meter moves while the song plays
    When the engine reports a level on one track
    Then that track's rendered meter lights
    When the level falls to nothing
    Then the meter is dark

  Scenario: Any number of tracks fit the mixer
    Given the song has nine tracks
    Then the mixer's strips scroll
    And the master strip stays inside the window

  Scenario: Deleting a track takes its clips with it
    Given a track with a clip on it
    When the producer deletes the track
    Then the track and its clip are gone and the audio graph has one track fewer

  Scenario: The editing keys stand aside while text is typed
    Given the instrument browser's search field has the keyboard
    When the producer presses Down
    Then the selected step keeps its pitch
    Given the editors have the keyboard
    When the producer presses Down
    Then the selected step drops a semitone

  Scenario: Return rewinds the song that is heard
    Given the engine is playing from a point past the start of the song
    When the producer presses Return
    Then the audio engine's playhead is back at the start, not only the drawn one

  Scenario: Home rewinds the song that is heard
    Given the engine is playing from a point past the start of the song
    When the producer presses Home
    Then the audio engine's playhead is back at the start, not only the drawn one

  Scenario: Mute and solo answer to the keyboard
    Given a track is selected
    When the producer presses Ctrl+M
    Then that track is muted
    When they press Ctrl+L
    Then that track is soloed

  Scenario: A held key rings until it is let go
    When the producer holds down a key of the playable surface
    Then the note sounds from the press
    And it is still sounding after a tapped key would have stopped
    When the key is released
    Then the note is let go

  Scenario: The session knows when it has unsaved changes
    When the producer saves the session
    Then it is clean and the window title carries no unsaved-changes dot
    When they edit a step
    Then the session is dirty and the title shows the dot
    When they save again with Ctrl+S
    Then it is written to the same file without asking where
    When they edit and then undo back to the saved state
    Then the session is clean again

  Scenario: The engine runs at the rate the audio device negotiated
    Given an audio device that runs at 44.1 kHz when 48 kHz is asked for
    When the session opens with its default instrument
    Then the engine renders at 44.1 kHz
    And a bar at 120 BPM lasts two seconds of that device's samples

  Scenario: A new session starts empty
    Given a session with unsaved changes
    When the producer starts a new session
    Then it has one empty pattern on one track, with the old first track's instrument
    And it has no history, no file, and nothing unsaved
