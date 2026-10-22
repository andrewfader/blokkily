Feature: The window says what went wrong, and what finished
  What the producer needs to know - a save that failed, a plugin a song names
  that would not load, an export that finished - is said in a notice at the
  foot of the window, over the editors, whichever side bars are hidden. It
  names the file or the plugin and why. A failure or a warning stays until its
  × is pressed; a finished piece of work goes by itself after a moment. The
  same thing is said once, not again at every rebuild.

  # bdd_notices
  Scenario: A save that fails is said, and stays
    When the song is saved somewhere that cannot be written
    Then a notice at the foot of the window names the file and why, readable in full
    And it is still there after a finished notice would have gone
    When its × is pressed
    Then it is gone

  # bdd_notices
  Scenario: A finished save is said and goes by itself
    When the song is saved
    Then a notice says it was saved, with the file's name
    And it goes by itself

  # bdd_notices
  Scenario: A plugin the song names that will not load is named once
    Given a song whose GHOST LEAD track plays a plugin that is not on this machine
    When it is opened
    Then a warning names the track and the plugin
    And loading the plugins again, still missing, does not say it again
