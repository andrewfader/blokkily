Feature: The window's bars auto-hide, and its panels move and size
  The left bar (devices, plugins, project) and the right bar (the mixer)
  auto-hide as they ship: each tucks into a strip at the window's edge while
  the editors are worked, and slides out over them when wanted. Intelligently:
  resting on the strip brings it out, passing over it does not; it stays while
  the pointer is over it, while something in it has the keyboard, while a
  press that began in it is held, and while a menu is open; a window with room
  for both bars beside the editors docks them anyway. The editors' panels move
  by the grip in the margin to their left, the keyboard is sized by the gap
  under it, and a bar is widened by its inner edge. All of it is remembered,
  and RESET in SETTINGS puts it back.

  # bdd_window_layout
  Scenario: The bars are tucked away at first
    Given a 1280 px window with the settings Blokkily ships with
    Then the left and right bars are strips at the window's edges, and the editors have the room between them

  # bdd_window_layout
  Scenario: Resting on the edge brings a bar out, and leaving sends it back
    When the pointer rests on the left strip
    Then the left bar slides out over the editors at a usable width, without moving them
    And it stays while the pointer is over it, and while its plugin search has the keyboard
    When the pointer has gone to the editors and nothing in the bar is in use
    Then it tucks away again
    And a pointer only passing over the strip does not bring it out

  # bdd_window_layout
  Scenario: Keys and the tour bring the bars out
    When Ctrl+] is pressed
    Then the mixer is out until the pointer has been in it and left
    And Ctrl+F brings the left bar out with its search
    And while the tour runs both bars are out for it to point at

  # bdd_window_layout
  Scenario: Room for everything, or auto-hide off, docks the bars
    When the window is wide enough for both bars beside the editors
    Then both bars are docked, and the editors are narrower by their widths
    When the left bar's auto-hide is switched off in SETTINGS at 1280 px
    Then the left bar is docked and the right bar is still a strip

  # bdd_window_layout
  Scenario: Panels move and size
    When the keyboard's grip is dragged above the arrangement
    Then the keyboard is laid out first, the arrangement follows it, and the order is remembered
    When the gap under the keyboard is dragged down
    Then the keyboard is taller by as much, and a double-click gives it back the height its keys want
    When the docked mixer's inner edge is dragged
    Then the mixer is wider by as much and the width is remembered
    When RESET is pressed in SETTINGS
    Then the panels are in their shipped order and the bars auto-hide again
