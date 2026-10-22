Feature: Tracks can be put in another order
  A track's header on the arrangement, dragged up or down, moves the track to
  where it is let go, a line marking the place while it is dragged; Move up and
  Move down in its menu step it one place. Its clips, its parts of every
  pattern, its mixer strip, its effects and everything that names it (keys,
  lanes, followers, launcher cells) go with it, and its running instrument
  moves with it rather than loading again. One step of history.

  # bdd_edit_once_see_everywhere
  Scenario: A header dragged up moves its track
    Given four tracks, the last called KEYS TWO
    When KEYS TWO's header is dragged up three rows, past a line marking the first place
    Then KEYS TWO is the first track, and the others keep their order after it
    And the instrument playing it is the same instance it was, not loaded again
    When it is undone
    Then the tracks are in their order before

  # bdd_edit_once_see_everywhere
  Scenario: Move down from the menu
    When Move down is chosen in the first track's menu
    Then it and the second track change places, and undo puts them back

  # program_schema
  Scenario: Everything that names a track follows it
    See program_schema.feature: Moving a track re-indexes everything that names a track
