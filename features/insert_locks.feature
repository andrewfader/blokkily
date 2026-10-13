Feature: A step lock can move one of the track's insert effects
  A step's parameter lock sets (automation) or offsets (modulation) a
  parameter from that step on. It can target the track's instrument or any
  of the track's insert effects. The step inspector's ON picker chooses which
  (INSTRUMENT, FX 1, FX 2...) and its PARAM picker chooses the parameter.
  The tracker marks a lock on an effect with that effect's number (F1, F2...)
  before its kind. Before a step's first lock the effect plays its own value.
  Removing an effect removes its locks, and locks on the effects after it
  move with those effects.

  The engine, bounce, project and model scenarios are proved by
  blokkily_insert_locks_tests (tests/insert_locks_tests.cpp, CTest
  insert_locks_<case>) from audio rendered through the production callback
  by the real CLAP effect fixture, whose output is its input times its Gain
  (0.25 until told otherwise). The interface scenario is proved by the
  bdd_insert_locks gate (`blokkily --verify --scenario insert_locks`, in
  src/app/verify/scenario_insert_locks.cpp), which saves
  build/artifacts/insert-locks.png.

  Not done: a launched scene cell plays its instrument's locks only. A lock
  sets its value from its step on; removing it leaves the parameter where the
  last lock put it until the song is reloaded, as with an instrument lock.

  # insert_locks_heard
  Scenario: A step lock on an insert effect is heard from its step on
    Given a steady signal through the CLAP effect, with locks on its Gain of 0.5 at sample 4000 and 0.1 at 8000
    When the song plays
    Then the effect plays its own Gain of 0.25 before the first lock
    And 0.5 from the first lock to the second, and 0.1 after it, the second landing 4000 samples after the first

  # insert_locks_modulation
  Scenario: A modulation lock on an insert offsets its parameter
    Given the same steps locking the Gain as modulation
    Then the Gain is 0.25, then 0.25 + 0.5, then 0.25 + 0.1

  # insert_locks_instrument_untouched
  Scenario: A lock on an insert leaves the instrument alone
    Given one step that locks the CLAP synth's Level to 0.9 and the effect's Gain to 0.5, both parameter 0
    Then the track is heard at 0.9 x 0.5

  # insert_locks_bounce
  Scenario: Insert locks are bounced as they are heard
    When the song with insert locks is bounced
    Then the file read back is what playback rendered, sample for sample, the Gain 0.25, then 0.5, then 0.1

  # insert_locks_project
  Scenario: Insert locks survive a save and a load
    When a song with modulation locks on an insert is saved and loaded
    Then each lock comes back on its insert, as modulation, and the song renders exactly as before
    And a song whose locks are all on instruments writes no lockslot record

  # insert_locks_remove_insert
  Scenario: Removing an insert takes its locks with it
    Given locks on a track's first and second effects
    When the first effect is removed
    Then its locks go, and the second effect's lock follows it to the first slot and is heard there

  # bdd_insert_locks
  Scenario: The inspector points a step's lock at an effect
    Given the CLAP synth on track 0 through the CLAP effect, and a held note on step 1
    When ON is set to the effect and PARAM to its Gain in the rendered inspector
    Then the step's lock is the effect's Gain at 0.5, and the tracker reads F1A...
    And played from that step the track is heard at twice its unlocked level
    And undoing both picks leaves the step with no lock, and redoing them is heard at the locked level again
