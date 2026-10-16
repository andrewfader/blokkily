Feature: Files open in the window that is already running
  While Blokkily runs, a .blok opened from the file manager (or
  `blokkily song.blok` typed in a terminal) is handed to the window that is
  open instead of starting another. That window comes forward and asks
  before replacing unsaved work. `blokkily --new-window song.blok` still
  opens a window of its own. The first window to start owns the channel; a
  socket a crashed window left behind is taken over.

  # bdd_open_running_stale_socket, bdd_open_running
  Scenario: A second launch hands its file to the running window
    Given a running window with unsaved work, which took over a stale socket
    When blokkily is started again with a project path relative to its folder
    Then the second launch exits at once without a window
    And the running window asks before discarding its work
    When the producer presses Discard
    Then the running window holds the project

  # bdd_open_running
  Scenario: A clean window opens the file without asking
    Given a running window with nothing unsaved
    When blokkily is started with the project's file:// URL
    Then the running window opens it straight away
