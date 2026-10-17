Feature: Files open in the window that is already running
  While Blokkily runs, a .blok opened from the file manager (or
  `blokkily song.blok` typed in a terminal) is handed to the window that is
  open instead of starting a second process that knows nothing of it. That
  window never replaces a song it holds: it opens the file itself only while
  it holds nothing yet, comes forward when it already holds that file, and
  otherwise opens the file in a new window beside it
  (features/multiple_windows.feature). `blokkily --new-window song.blok`
  always opens a window of its own. The first window to start owns the
  channel; a socket a crashed window left behind is taken over.

  # bdd_open_running_stale_socket, bdd_open_running
  Scenario: A second launch hands its file over and a song with unsaved work stays
    Given a running window with unsaved work, which took over a stale socket
    When blokkily is started again with a project path relative to its folder
    Then the second launch exits at once without a window of its own
    And a new window opens holding the project
    And the running window keeps its unsaved work without asking anything

  # bdd_open_running
  Scenario: A saved song stays too
    Given a running window holding a saved song
    When blokkily is started with another project's file:// URL
    Then that project opens in a new window and the running one keeps its song

  # bdd_open_running
  Scenario: The file a window holds brings it forward
    Given a running window holding a saved song
    When blokkily is started with that song's own file
    Then no new window opens and the running window is unchanged

  # bdd_open_running
  Scenario: A window holding nothing yet opens the file itself
    Given a running window with an untitled song and nothing unsaved
    When blokkily is started with a project path
    Then the running window opens it straight away, without asking
