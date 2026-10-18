Feature: A step lock can move an insert effect: the track's, a return's or the master's
  A step's parameter lock sets (automation) or offsets (modulation) a
  parameter from that step on. It can target the track's instrument, any of
  the track's insert effects, or any insert effect of a return or of the
  master bus. The step inspector's ON picker chooses which (INSTRUMENT,
  FX 1, FX 2..., RETURN A · FX 1..., MASTER · FX 1...) and its PARAM picker
  chooses the parameter. The tracker marks a lock on an effect with that
  effect's number (F1, F2...; RAF1 for return A's first, MF1 for the
  master's first) before its kind. Before a step's first lock the effect
  plays its own value. Removing an effect removes its locks, and locks on the
  effects after it move with those effects; removing a return removes its
  locks, and locks on later returns follow them.

  The engine, bounce, project and model scenarios are proved by
  blokkily_insert_locks_tests (tests/insert_locks_tests.cpp, CTest
  insert_locks_<case>) from audio rendered through the production callback
  by the real CLAP effect fixture, whose output is its input times its Gain
  (0.25 until told otherwise). The interface scenario is proved by the
  bdd_insert_locks gate (`blokkily --verify --scenario insert_locks`, in
  src/app/verify/scenario_insert_locks.cpp), which saves
  build/artifacts/insert-locks.png.

  When the last lock on a parameter goes, the parameter is put back where it
  was before it was first locked: a value lock's parameter to the value the
  plugin held then, or the value the producer last turned it to in the
  plugin's own window while it was locked; a modulation lock's offset to
  nothing. This holds for
  instruments and inserts alike, for plugins that report their parameter
  values (CLAP, VST3, LV2, the sampler and the built-in effects).

  A launched scene cell plays all of its locks, its instrument's and its
  inserts', a return's and the master's, as the arrangement does. Not done: a
  lock sets its value from its step on.

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

  # insert_locks_removed_restores
  Scenario: Removing a lock puts its parameter back
    Given the CLAP synth (Level 0.25 of its own) through the CLAP effect (Gain 0.25 of its own), with a held key whose step locks the Level to 0.9 and the Gain to 0.5
    When the song plays it is heard at 0.9 x 0.5
    And when the Gain's lock becomes a modulation of +0.5 it is heard at 0.9 x (0.25 + 0.5)
    And when every lock is removed, in two edits recompiled before the engine plays either, it is heard at 0.25 x 0.25, as before it was locked

  # insert_locks_bus_locks
  Scenario: A step lock can move a return's or the master's insert
    Given a track heard only through return A, which carries the CLAP effect, as does the master (Gain 0.25 of their own)
    And the track's steps lock return A's Gain to 0.5 at sample 4000 and the master's to 0.75 at 8000
    When the song plays
    Then the master hears twice the level from the first lock and six times it from the second

  # insert_locks_launched
  Scenario: A launched cell plays its insert locks
    Given the CLAP effect on track 0 and on the master, and an arrangement that plays nothing
    And a cell on track 0 that locks the track's Gain to 0.5 at 4000 and the master's to 0.75 at 8000
    When the cell is launched and the song plays
    Then the level doubles from 4000 and is six times its own from 8000

  # insert_locks_bus_locks_model
  Scenario: Locks on a return or the master are saved, and go with what they lock
    When the song is saved and loaded
    Then each lock comes back on its return or the master, and the text round-trips byte for byte
    And removing the master's effect takes its lock and leaves the return's
    And removing return A takes its lock, and return B's lock follows it to the first return
    And a lock on a return that does not exist is refused

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
    And undoing both picks leaves the step with no lock, heard at its unlocked level again, and redoing them is heard at the locked level again
    When the lock is cleared and ON is set to the master's CLAP effect and PARAM to its Gain
    Then the step's lock reads MF1A... and the master is heard at twice its unlocked level

  # bdd_insert_locks
  Scenario: A value turned by hand while a parameter is locked is kept
    Given the step's lock holds the effect's Gain at 0.5
    When the producer turns the Gain to 0.75 in the effect's own window
    Then played from the step the lock still holds it, at twice the unlocked level
    When the lock is cleared
    Then the step is heard at three times its unlocked level, the Gain the producer left
