Feature: A pattern can swing its even sixteenths
  SWING on the open pattern delays the even sixteenths when they play, so a
  straight grid can shuffle without rewriting the notes. 0% is straight; 100%
  delays those steps by half a sixteenth. The editors keep the grid. Swing is
  every track's part of the pattern, or with Ctrl held one track's alone; it
  is saved with the pattern and is one undo step.

  The engine scenarios are proved by blokkily_swing_tests
  (tests/swing_tests.cpp). The interface scenarios are proved by the
  bdd_pattern_swing gate (`blokkily --verify --scenario pattern-swing`),
  which hears the shuffle through the production render callback from the
  CLAP fixture and saves build/artifacts/pattern-swing.png.

  # swing_scheduler_delays_offbeats
  Scenario: The scheduler delays even sixteenths
    Given a one-bar pattern with a note on the first sixteenth and one on the second
    And swing at 100%
    When the pattern is scheduled
    Then the first note stays on the downbeat
    And the second note starts half a sixteenth later

  # swing_off_is_straight
  Scenario: With swing at 0% the grid plays straight
    Given the same pattern with swing at 0%
    When the pattern is scheduled
    Then both notes start on their sixteenths

  # swing_heard
  Scenario: Playback hears the delayed sixteenth
    Given the swung pattern on a track through the CLAP fixture
    When the arrangement plays
    Then the second note sounds later than it does with swing off
    And an export of the swung song matches that playback

  # swing_project
  Scenario: Swing is saved with the pattern
    Given a pattern with swing at 67%
    When the project is saved and loaded
    Then that pattern's swing is 67%
    And a file without a swing record loads straight

  # swing_malformed
  Scenario: A malformed swing record is refused
    Then a percent outside 0 to 100 is refused
    And a second swing record for the same pattern is refused
    And a swing that names a missing pattern is refused

  # bdd_pattern_swing
  Scenario: SWING sits beside the pattern length
    Given the window with a pattern of two sixteenths
    Then SWING is usable and reads 0%
    When it is set to 100%
    Then playback hears the second step late
    And undo puts the grid back to straight

  # bdd_pattern_swing
  Scenario: One track can swing on its own
    Given the pattern on two tracks, both swung 100%
    When the second track is selected and Ctrl-Shift-clicking SWING's − takes it to 0%
    Then only that track's part is straight, and the readout says 0%* while the tracks differ
    And the first track still reads 100%*
    When SWING's − is clicked without Ctrl
    Then every track swings 99% and the mark is gone
