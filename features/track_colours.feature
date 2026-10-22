Feature: A track can be given a colour
  COLOUR in a track's menu (right-click its header or its mixer strip) gives
  it one of eight colours, or back to Automatic. Its clips on the arrangement
  are drawn in it, and its header and mixer strip carry a bar of it, so a
  track is found by its colour at a glance. A track not given one is drawn
  as before. The colour is one step of history and is saved with the song.

  # bdd_edit_once_see_everywhere
  Scenario: A colour from the track's menu
    When Green is chosen under COLOUR in a track's menu
    Then the track's header and mixer strip carry a green bar, and its clips are green
    When it is undone
    Then the track is drawn as before

  # project_format
  Scenario: A track's colour is saved with it
    Given a song whose second track is green
    When it is saved and opened again
    Then the second track is green and the first has no colour of its own
    And a song with no colours writes no colour records
    And a malformed colour, one for a missing track, or two for one track, is refused
