Feature: A song keeps notes and named versions
  Two views sit beside ALL, STEP, TRACKER, PIANO, KEYS and LAUNCH. NOTES holds
  free text about the song (lyrics, ideas, what the next session is for). It
  is saved with the song, and each burst of typing is one step of history.
  VERSIONS keeps the song as it was at moments the producer names: SAVE
  VERSION stores the whole song, with what every processor sounds like, and
  RESTORE puts a version back in the song's place as one step of history, so
  UNDO brings back the song it replaced. Versions are history kept beside the
  song as project text, not a second song: nothing plays or edits one until it
  is restored, and undo never makes or removes one. They are saved in the
  project file (additive `notes` and `version` records), newest shown first.
  DELETE asks twice, because a deleted version cannot be undone.

  The file scenarios are proved by blokkily_versions_tests
  (tests/versions_tests.cpp, CTest versions_<case>). The interface scenarios
  are proved by the bdd_versions gate (`blokkily --verify --scenario
  versions`), which hears a restored version through the production render
  callback from the CLAP fixture and saves build/artifacts/versions.png.

  # versions_project
  Scenario: Notes and versions are saved with the project
    Given a song with notes over two lines and two versions
    When the project is saved and loaded
    Then the text round-trips byte for byte, the notes and every version come back in order
    And a version opens as the song it was, with no versions of its own

  # versions_unchanged
  Scenario: A project with neither is unchanged
    Then a song with no notes and no versions writes neither record

  # versions_malformed
  Scenario: A malformed version or a second notes record is refused
    Then a version without its time and snapshot, or saved before 1970, is refused
    And a second notes record is refused

  # bdd_versions "versions: VERSIONS and NOTES sit with the other views"
  # bdd_versions "versions: NOTES keeps the song's notes, one step of history"
  Scenario: NOTES keeps the song's notes
    Given the window with the VERSIONS and NOTES tabs, and RESCAN still inside it
    When NOTES is clicked and "hook in bar nine" typed
    Then the editors give the notes their place, and the song's notes read "hook in bar nine"
    And one undo takes the typing back and redo brings it back

  # bdd_versions "versions: SAVE VERSION keeps the song as it is, newest first"
  # bdd_versions "versions: RESTORE brings a version back, heard as it was"
  # bdd_versions "versions: undo takes a restore back, never a version"
  Scenario: A version is saved and restored
    Given a note on step 1 heard through the CLAP synth at 120 BPM
    When the producer names a version FIRST and saves it
    And clears the note, sets 90 BPM and saves another
    Then the list shows VERSION 2 above FIRST
    When FIRST is restored
    Then the note is there and heard as before, at 120 BPM, and both versions remain
    And undo brings back the song without the note at 90 BPM, and leaves both versions

  # bdd_versions "versions: versions and notes survive a save and a load"
  # bdd_versions "versions: DELETE asks twice"
  Scenario: Versions are saved with the project and deleted with care
    When the project is saved and opened again
    Then both versions and the notes are there
    When DELETE is pressed on a version
    Then it asks SURE? and the version stays until it is pressed again
