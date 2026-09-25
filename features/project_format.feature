Feature: Project files keep opening as the program grows
  Every feature that stores something in a project adds records of its own
  instead of changing the lines that are already there, so a project saved by
  an earlier build opens in a later one exactly as it was saved.

  Scenario: A project saved in format 4 still opens
    Given a project file written by a build that saved format 4
    When the producer opens it
    Then its patterns, triggers, locks, tracks, clips and tuning all come back
    And saving it writes the same records under the format 5 header

  Scenario: Saving the same project twice gives the same bytes
    Given a project opened from a format 5 file
    When the producer saves it twice in a row
    Then both files are byte-identical to each other and to the file opened

  Scenario: A file from before the song model is refused
    Given a project file that declares format 3, or a format newer than this build
    When the producer opens it
    Then it is refused with a reason rather than half-loaded

  Scenario: The legacy tempo line is read in every format
    Given a format 4 or format 5 project with a "tempo" line
    When the producer opens it
    Then the song plays at that tempo

  Scenario: A file inside the project folder is referenced relative to it
    Given a project saved in a folder
    And a file referenced from inside that folder
    When the reference is written
    Then it is stored relative to the project folder
    And reading it back names the same absolute file
    And a file outside the folder keeps its absolute path
