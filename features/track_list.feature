Feature: The arrangement's track list is sized by hand, and mutes from the keyboard and its menu
  The track names down the side of the arrangement are as wide as their right
  edge is dragged, so long names can be read and short ones leave the bars the
  room. The width is remembered; a double-click on the edge or RESET in
  SETTINGS gives back the width it ships with.

  A click on a track's name gives the list the keyboard: M then mutes that
  track, and M again brings it back. M is otherwise the tracker's note key, so
  the list hands the keyboard back at the next press anywhere, step moved or
  note typed. The right button on a name opens the track's menu with Mute first.

  # bdd_track_list
  Scenario: The list's edge sizes it
    Given a song with two tracks, the first with a clip in its second bar
    When the list's right edge is dragged 100 px to the right
    Then the track headers are 100 px wider, and the bars have the rest of the view
    And the ruler, the clips and audio clips still line up, and a click on a bar still lands on that bar
    And the width is remembered
    When the edge is dragged past the most it can take
    Then the list stops at half the view at most, and the bars keep the rest
    When the edge is double-clicked
    Then the list is as wide as it shipped

  # bdd_track_list
  Scenario: M mutes a track whose name was clicked
    Given the song playing a note on its first track
    When the first track's name is clicked and M is pressed
    Then the track is muted, its header says so, and nothing is heard
    When M is pressed again
    Then the track sounds again, and undo takes the two mutes back

  # bdd_track_list
  Scenario: M is still a note everywhere else
    Given a track's name was clicked
    When Z is typed and then M
    Then both are notes on the part, and the track is not muted
    Given a track's name was clicked again
    When a tracker row is moved to or a bar is pressed, and M is typed
    Then M is a note, and the track is not muted

  # bdd_track_list
  Scenario: The right button mutes a track
    When the right button is pressed on the first track's name
    Then its menu opens with Mute at the top
    When Mute is clicked
    Then the track is muted, nothing is heard, and the menu's Mute is ticked
