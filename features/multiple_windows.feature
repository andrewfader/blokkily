Feature: Several songs open at once, each in a window of its own
  A producer can keep two songs open side by side: a sketch and the mix it
  borrows from, or two versions to compare. Each window is a Blokkily process
  of its own, with its own song, audio stream, undo history and autosave, so
  opening, closing or crashing one leaves the others as they were.

  # bdd_multiple_windows
  Scenario: + WINDOW opens an empty song beside this one
    Given a window holding a song with unsaved changes
    When the producer presses + WINDOW on the project rail
    Then another window opens holding an untitled song with nothing unsaved
    And the first window still holds its song and its unsaved changes
    And nobody is asked to discard anything

  # bdd_multiple_windows
  Scenario: OPEN NEW opens a project beside this one
    Given a window holding a song with unsaved changes
    When the producer presses OPEN NEW and chooses a project
    Then another window opens holding that project, titled with its name
    And the first window still holds its song and its unsaved changes

  # bdd_multiple_windows
  Scenario: Opening in this window still asks first
    Given a window holding a song with unsaved changes
    When the producer presses Ctrl+O
    Then the window asks before discarding its work

  # bdd_multiple_windows
  Scenario: A song open in one window is never opened in another
    Given a window holding a project, beside a window with unsaved work
    When the producer opens that project again in a new window
    Then the window holding it comes forward and no second copy opens
    When the project is opened from the desktop as well
    Then the window holding it comes forward again

