Feature: Playback can loop a stretch of the arrangement
  LOOP on the arrangement wraps playback from the OUT bar back to the IN bar,
  so a phrase can be heard over and over without stopping the transport or
  rebuilding the audio graph. IN is the first bar of the loop; OUT is the bar
  it wraps at. Mixer moves still reach the running engine. An export still
  renders the whole arrangement from its start, with the loop off for that
  bounce. The region is saved with the project and is not a step of undo.

  The engine scenarios are proved by blokkily_loop_tests
  (tests/loop_tests.cpp). The interface scenarios are proved by the
  bdd_arrangement_loop gate (`blokkily --verify --scenario arrangement-loop`),
  which hears the loop through the production render callback from the CLAP
  fixture and saves build/artifacts/arrangement-loop.png.

  # loop_wraps_playback
  Scenario: Playback wraps at OUT back to IN
    Given a song with a note in bar 1 and another in bar 3, at 120 BPM
    And the loop set from bar 3 to bar 4
    When playback starts at the loop
    Then the note in bar 3 sounds on every pass
    And the note in bar 1 is silent for those passes

  # loop_off_plays_the_song
  Scenario: With LOOP off the song plays through
    Given the same song with the loop off
    When playback starts at the beginning
    Then the note in bar 1 sounds, then the note in bar 3

  # loop_mixer_live
  Scenario: A mute still reaches the mix while the loop plays
    Given the loop playing the note in bar 3
    When that track is muted
    Then the looped note is silent, and unmuting brings it back

  # loop_bounce_is_the_arrangement
  Scenario: An export is the whole arrangement
    Given the loop on from bar 3 to bar 4
    When the arrangement is bounced
    Then the file has the note in bar 1 at the start, then the note in bar 3
    And it matches a bounce of the same song with the loop off

  # loop_project
  Scenario: The loop is saved with the project
    Given a loop on from bar 2 to bar 4
    When the project is saved and loaded
    Then the loop is on from bar 2 to bar 4
    And a file without a loop record loads with the loop off

  # loop_malformed
  Scenario: A malformed loop record is refused
    Then a loop whose end is not after its start is refused
    And a second loop record is refused

  # bdd_arrangement_loop
  Scenario: LOOP, IN and OUT are on the arrangement
    Given the window with the arrangement
    Then LOOP, IN and OUT are each usable
    When IN is set on bar 3 and OUT on bar 4 and LOOP is on
    Then the ruler marks those bars
    And playback from bar 3 hears that bar's note twice and not bar 1's
    And an export still begins with bar 1's note
