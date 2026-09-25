Feature: Every voice of a chord sounds as hard and as long as it was played
  A chord is one step with several voices. When keys are recorded onto a step
  that already sounds, the step becomes a chord, and each of its voices keeps
  the velocity it was struck at and the time it was held, so a soft short key
  and a hard long one still sound that way when the song plays them back. A
  chord struck as one, from a chord pad, has one velocity for every voice. The
  tracker and the inspector show and edit how hard a chord is struck, and the
  inspector edits each voice on its own.

  The engine, take and file scenarios are proved by blokkily_chord_velocity_tests
  (tests/chord_velocity_tests.cpp), one CTest test per case, from audio rendered
  through the production callback by the real CLAP fixture (its velocity mode,
  parameter 2, sounds 0.25 × the sum of the velocities it holds) and by
  FluidSynth playing a real SoundFont. The interface scenarios are proved by
  the bdd_chord_velocity gate (`blokkily --verify --scenario chords`, in
  src/app/verify/scenario_chords.cpp), which names the scenario it failed in
  ("BDD FAIL at: chords: …") and saves build/artifacts/chord-velocity.png. The
  real-time case realtime_chord_velocity proves the render callback plays such
  a chord without allocating.

  Scenario: Keys merged onto one step keep how hard and how long each was played
    # chord_velocity_write_played (a regression: the merge used to play every
    # voice at 0.8 for the longest voice's length)
    Given a step that sounds D4 at velocity 0.6 for 56 ticks
    When a take plays E4 a quarter tone sharp at velocity 0.9 for 300 ticks on that step
    Then the step is a chord of both whose voices sound at 0.6 and 0.9
    And they last 56 and 300 ticks, and the step lasts as long as its longest voice
    And a third key merged in keeps its own velocity and length too
    And a chord pad's step that gains a played key keeps the pad's velocity for its own voices

  Scenario: Each voice sounds as hard and as long as it was played
    # chord_velocity_scheduler_clap, realtime_chord_velocity
    Given a chord whose voices are struck at 0.6, 0.9 and 0.2 and held for 800, 400 and 200 ticks
    When the song plays it through the CLAP fixture
    Then the rendered level is 0.425 while all three sound
    And it steps down to 0.375 and then 0.15 as the shorter voices end on their own
    And the render callback does not allocate while it plays

  Scenario: Editing one voice changes only that voice
    # chord_velocity_scheduler_clap
    Given that chord is playing
    When the quietest voice is raised to 0.8 while the song runs
    Then the next loop is heard at 0.575 while all three sound
    And the other voices sound as they did

  Scenario: A SoundFont hears each voice's velocity
    # chord_velocity_soundfont
    Given a C4 and G4 chord played by FluidSynth from a real SoundFont
    When it is rendered with C4 loud and G4 soft, and again the other way round
    Then the balance between C4 and G4 in the rendered audio flips by more than four times

  Scenario: A chord saved before chord velocity plays as it always did
    # chord_velocity_legacy_chord
    Given a format 4 project whose chord line says nothing about velocity
    When it is opened and played through the CLAP fixture
    Then every voice sounds at 0.8 for the length of its step

  Scenario: Chord velocities and lengths survive a save and a load
    # chord_velocity_project
    Given a chord with per-voice velocities and lengths, a chord pad's chord and a plain chord
    When the project is saved
    Then the chord lines keep their format 4 shape
    And voicevel and voicelen records carry what differs from the default
    And a plain chord writes neither
    When it is loaded again
    Then every chord is as it was and saving it again gives the same bytes
    And a voice record naming a note, an unknown step, the wrong number of voices,
      a velocity outside 0 to 1 or a length that is not positive is refused with a reason

  Scenario: A recorded chord shows its voices
    # bdd_chord_velocity: "chords: a take merged onto a step keeps each voice's velocity and length"
    Given recording is armed on the CLAP track
    When two keys go down together on an empty step at velocities 40 and 120
    And the softer one is let go well before the other
    Then the step is a chord whose voices are at 40 and 120, the first shorter than half the second
    And the tracker's VEL column shows the chord's loudest voice
    # "chords: the chord is heard at its voices' velocities"
    And played back through the CLAP fixture it is heard at 0.25 × (40 + 120) / 127

  Scenario: The inspector edits one voice
    # bdd_chord_velocity: "chords: the inspector shows each voice",
    # "chords: dragging one voice changes only that voice"
    Given the recorded chord is selected
    Then the inspector shows a bar for each voice, each at least 18 by 48 pixels and inside the window
    When the producer drags the first voice's bar to the top
    Then that voice is at 127 and the other still at 120
    And the step is heard at 0.25 × (1 + 120 / 127)

  Scenario: One drag is one step of history, and it is saved
    # bdd_chord_velocity: "chords: undo and redo", "chords: save and load"
    When the producer undoes
    Then the voice is back at 40 and the chord is heard as recorded
    When they redo
    Then it is at 127 again
    When the project is saved, the edit undone, and the file opened
    Then the chord's voices are at 127 and 120 with the lengths they were recorded with
    And it is heard at 0.25 × (1 + 120 / 127)
