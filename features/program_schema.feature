Feature: A song keeps its audio, effects, inputs and automation
  A song holds everything a session needs beyond patterns: the audio files it
  plays and where their clips sit, the effects on every bus and the sends that
  feed the returns, what each track records from, and the automation that
  moves its controls. All of it is saved with the project, and removing a
  track takes with it exactly what belonged to that track.
  Executable scenarios: tests/schema_tests.cpp (CTest program_schema).

  Scenario: A song using every new field saves and reopens unchanged
    Given a song with audio files, audio clips, return buses, insert effects on a track, a return and the master, sends, an armed input, a recording offset and automation lanes in every mode
    When the producer saves it, reopens it and saves it again
    Then both saves are byte-identical to the expected records
    And every field reads back as it was written

  Scenario: An audio file inside the project folder is stored relative to it
    Given a song whose audio file lies inside the project folder
    When the project is saved into that folder
    Then the audiofile record names the file relative to the folder
    And reopening the project names the same absolute file

  Scenario: A project from before these records opens with their defaults
    Given a project file with none of the new records
    When the producer opens it
    Then it has no audio, no returns, no inserts and no sends, every input is unarmed on MIDI, and every track reads its automation
    And saving it writes none of the new records

  Scenario: A damaged record is refused with a reason
    Given a project file with a malformed or dangling audio, effect, input or automation record
    When the producer opens it
    Then it is refused with a message naming what is wrong rather than half-loaded

  Scenario: Overlapping audio clips are allowed
    Given two audio clips on one track that overlap in time
    Then the song is consistent

  Scenario: The song refuses references that do not resolve
    Given a song where a clip, send, effect slot, input or automation lane breaks a rule
    Then the song is not consistent and says which rule failed

  Scenario: Removing a track re-indexes everything that names a track
    Given three tracks with pattern clips, audio clips, sends and automation lanes
    When the producer removes the middle track
    Then its clips, audio clips and the lanes targeting it are gone
    And everything on the last track now names the second track
    And the old-to-new index map says so

  Scenario: Unused audio files are dropped when saving
    Given a song listing an audio file no clip plays
    When its unused files are pruned
    Then only files a clip plays remain and the clips still name them

  Scenario: An automation lane is a ramped envelope with jumps
    Given a lane with points
    Then between points it ramps, before the first and after the last it holds, and at a jump it takes the leaving value

  Scenario: Recording a pass replaces only the range it covers
    Given a lane with a ramp
    When a pass is written over part of it
    Then inside the range the lane follows the pass
    And outside the range it is unchanged, with jumps where the pass meets it

  Scenario: Thinning keeps the shape of the envelope
    Given a densely recorded lane
    When it is thinned with a tolerance
    Then only the points the shape needs remain, jumps are kept, and every dropped point stays within the tolerance
