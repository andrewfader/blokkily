Feature: Clips are dragged, copied and pasted on the arrangement
  A clip pressed and let go where it was opens, as it always did. Dragged
  along its lane it moves to where it is let go, an outline marking the bars
  it would cover; dragged with Ctrl held it is copied there, the original
  staying. A clip chosen by a click is copied by Ctrl+C, and Ctrl+V pastes
  it at the playhead's bar on the selected track, as that track's part of the
  same pattern, where the bars are free. Each is one step of history.

  # bdd_edit_once_see_everywhere
  Scenario: A clip dragged along its lane moves there
    Given a clip at bar 3 and nothing at bars 6 and 7
    When it is dragged to bar 6, an outline marking where it would land
    Then it starts at bar 6 and bar 3 is empty
    And undo puts it back

  # bdd_edit_once_see_everywhere
  Scenario: A clip dragged with Ctrl is copied there
    When the clip at bar 3 is dragged to bar 6 with Ctrl held
    Then there is a clip of the same pattern at bar 3 and at bar 6

  # bdd_edit_once_see_everywhere
  Scenario: A clip copied and pasted onto another track
    Given the clip at bar 3 of the first track chosen by a click
    When Ctrl+C is pressed, the second track is selected, the playhead is put at bar 7 and Ctrl+V is pressed
    Then the second track has a clip at bar 7 playing its own part of the same pattern
