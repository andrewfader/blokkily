Feature: Autosave and crash recovery
  A song with unsaved changes is copied to a recovery folder every 30
  seconds (BLOKKILY_AUTOSAVE_MS changes how often), written so a crash while
  writing leaves the previous copy whole. Saving, opening or starting a song,
  or closing the window normally, removes the copy. A copy a window left
  behind when it did not close normally (a crash, a kill, a power cut) is
  offered back at the next launch. The folder is the user's data folder, or
  BLOKKILY_RECOVERY_DIR.

  # bdd_recovery_crash
  Scenario: Unsaved work is copied aside while it is worked on
    Given a running window with no unsaved changes
    Then nothing is copied
    When a track is renamed
    Then the autosave timer copies the song aside
    And the window does not offer its own copy to itself
    When the song is saved
    Then the copy is removed, and the next edit makes a new one
    When the window dies without closing
    Then the copy stays behind

  # bdd_recovery_restore
  Scenario: The next launch offers the song back
    Given a copy a crashed window left, and an older untitled one
    When Blokkily starts
    Then it offers the newest first, naming the project and when it was saved
    When the producer presses RECOVER
    Then the song is open as it was, unsaved, as the project it came from
    And the older copy is offered next, and DISCARD lets it go
    When the song is saved
    Then it is written to its project and no copy remains

  # bdd_recovery_restore
  Scenario: Two windows never recover the same song
    Given two windows started together, both offered a crashed window's copy
    When the other window takes the copy first
    Then this window can neither recover nor discard it, and stops offering it
    When the other window lets it go untouched
    Then it is offered here again
