Feature: A chord's inversion and strum are edited in the step inspector
  A chord step can be turned through its inversions and strummed. Both are
  edited in the step inspector beside probability, micro-timing and ratchets.
  An inversion turns the chord's voices in the pattern itself: up lifts the
  lowest voice an octave, down drops the highest. So the tracker, the piano
  roll and the inspector all show the voices that sound. Strum is how many
  ticks each voice starts after the one below it, from 0 to 60 (half a step).

  The scenarios are proved by the bdd_chord_voicing gate (`blokkily --verify
  --scenario voicing`, in src/app/verify/scenario_voicing.cpp). The gate clicks
  and drags the rendered inspector, reads the pitch and onset of each voice
  from audio rendered through the production callback by the real CLAP
  fixture in its tone mode (a sine at the last key it was sent), names the
  scenario it failed in ("BDD FAIL at: voicing: …") and saves
  build/artifacts/chord-voicing.png.

  Scenario: The inspector offers inversion and strum for a chord
    Given a C4 E4 G4 chord on one step, selected
    When the step inspector is shown
    Then its INV ▼ and INV ▲ chips and its STRUM dial are usable and inside the window
    And for a single note the inversion chips and the strum dial are disabled

  Scenario: Strum spreads a chord's voices out in time, lowest first
    Given the C4 E4 G4 chord, played through the CLAP fixture's tone mode
    When the STRUM dial is dragged to its end
    Then the inspector reads 60 TK
    And the rendered audio sounds C4, then E4 3000 samples (60 ticks) later, then G4 3000 samples after that
    And the drag is one step of history

  Scenario: Inverting a chord turns its voices in every editor and in the audio
    Given the strummed C4 E4 G4 chord
    When INV ▲ is clicked
    Then the step holds E4 G4 C5, and the tracker names it E4+2 and the piano roll draws keys 64, 67 and 72
    And the rendered audio sounds E4, then G4, then C5
    When INV ▼ is clicked twice
    Then the step holds G3 C4 E4, and the audio sounds G3, then C4, then E4
    And each click is one step of history: undo brings back E4 G4 C5, then C4 E4 G4

  Scenario: A voiced chord survives a save and a load
    Given the G3 C4 E4 chord with a 60-tick strum
    When the project is saved and loaded again
    Then the step still holds G3 C4 E4 with a 60-tick strum, and it sounds that way
