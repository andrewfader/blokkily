Feature: History is a tree that can be gone back along to any step
  UNDO and REDO step along history as they always did. An edit made after an
  undo no longer throws the steps undone away: it starts a branch beside
  them, and they stay. HISTORY (Ctrl+H, or a right-click on UNDO) lists every
  step in the order it was made, each named by what it changed, a branch
  indented under the step it went on from; the step the song is at is lit.
  A click on any step puts the song as it was there, whichever branch it is
  on, and redo then follows the way it was reached.

  # bdd_undo_history
  Scenario: An edit after an undo starts a branch
    Given the first track renamed ONE, then TWO, then turned down to -6 dB
    When two steps are undone and the track is turned down to -12 dB
    Then the song is ONE at -12 dB with nothing to redo
    And the history lists five steps, TWO and -6 dB on the first line and -12 dB on a branch under ONE

  # bdd_undo_history
  Scenario: Any step can be gone back to
    When the step that renamed it TWO is chosen
    Then the track is TWO at 0 dB, and redo turns it down to -6 dB
    When it is undone back to ONE and redone
    Then redo follows the line last taken, to TWO

  # bdd_undo_history
  Scenario: The HISTORY panel goes to a step
    When Ctrl+H is pressed
    Then the HISTORY panel lists the steps
    When the branch's step is clicked
    Then the track is ONE at -12 dB
    And Ctrl+H closes the panel
