Feature: Comping and punch recording
  PUNCH, beside LOOP, IN and OUT, records only what is played between IN and
  OUT, looped or not; audio input and MIDI alike. Looped over the punch
  range, every pass becomes a take of the same bars. Takes of the same bars
  on one track are a stack: one plays, the others are kept, comped out.
  After a recording the last pass plays. A clip's TAKE badge plays the next
  take of its stack instead; SPLIT cuts the selected track's takes at the
  playhead, so each section can play a different take. Clips that overlap
  without being comped out still sum, as before. Comping and punch are
  saved with the song; choosing and splitting are steps of history.
  MIDI passes recorded in a loop stack the same way while TAKES (beside
  PUNCH) is lit, which it is unless switched off: each pass is a take of its
  own over the loop's bars, the last plays and the others are kept, and the
  bars they were recorded over play on under them, as an overdub. With TAKES
  off a pass merges into the pattern there, as before.

  # comping_punch_audio
  Scenario: Punched audio starts and ends at the range
    Given a track recording its input from sample 1000, punched from 2000 to 4000
    Then its one take starts at 2000, is 2000 frames, and holds what was played there
    And looped from 1000 to 5000, every pass is a take of exactly the punch range

  # comping_punch_midi
  Scenario: Punched MIDI keeps only the notes inside
    Given a note struck in every block while recording, punched from 2048 to 4096
    Then only the four struck inside the range are captured
    And every note-off is, so no note hangs

  # comping_comp_passes
  Scenario: The last pass plays and the others are kept
    Given three passes of the same bars over a clip that was already there
    Then they and the clip are one stack
    And the two earlier passes are comped out, the last plays, and the old clip is untouched
    When the first pass is chosen
    Then it plays and everything it overlaps is comped out, and nothing else

  # comping_comp_render
  Scenario: The engine plays the takes chosen
    Given takes A and B of the same second
    Then uncomped they sum, and comped only B is heard
    When A is chosen, A is heard throughout
    When both are split at half a second and B chosen after the cut
    Then the export is A to the cut and B from it, sample for sample
    And a take chosen on both sides of a cut has no seam

  # comping_records
  Scenario: Comping and punch are saved
    Then comped_out and punch records save and load, a song without them saves neither, and a clip the file lacks is refused

  # bdd_comping
  Scenario: Comping in the window
    Given takes A and B of a second, B playing
    When the producer clicks B's TAKE badge
    Then A plays in the export, and one undo puts B back
    When the playhead is at half a second, SPLIT is clicked, and B chosen for the second half
    Then the export is A then B
    And PUNCH arms the range, and all of it survives a save and a load

  # comping_midi_takes
  Scenario: Looped MIDI passes are takes, the last plays
    Given two passes of the same bar, recorded over an empty part, a note on each half in each
    Then they are one stack, the earlier comped out, the last playing, and the part they were recorded over plays on and is in no stack
    And the engine plays the last pass (heard by its pitch through the CLAP synth); choosing the first plays it
    And split at the half bar with the second chosen after the cut, the first pass plays to the cut and the second from it

  # comping_midi_take_records
  Scenario: MIDI takes are saved
    Then comped-out take clips, take sections and TAKES off save and load, a song without them saves none, and a clip or section the file does not have is refused

  # bdd_midi_takes
  Scenario: Looped MIDI passes in the window
    Given the first bar looped over an empty part, and recording armed
    When three passes are played, a note early in the first, in the middle of the second and late in the third
    Then the bar holds three takes, the last plays (read from the track's meter), and the take sections are not offered as patterns
    When the clip's TAKE badge (3/3) is pressed
    Then the first pass plays, and one undo puts the last back
    When TAKES is switched off and another pass is played
    Then it merges into the pattern under the take, and no take is made
    And the takes, which plays, and TAKES off survive a save and a load

