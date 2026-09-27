Feature: Non-Linear Clip / Scene Launcher Matrix
  As a music producer and live performer
  I want a non-linear grid of clip slots grouped into scenes with launch quantization and follow actions
  So that I can improvise, sketch song structures, and jam non-linearly into the arrangement timeline.

  Scenario: Clip and scene launching respects bar quantization
    Given a song with two tracks and two scenes in the launcher matrix
    And Scene 0 has Pattern 0 on Track 0 and Pattern 1 on Track 1
    When Scene 0 is launched with bar quantization while transport is playing
    Then both tracks queue their clips and begin sounding at the next bar boundary.

  Scenario: Follow actions automatically transition scenes
    Given a clip slot configured with 1 repeat and follow action set to next
    When the pattern finishes its repetition
    Then the slot in the next scene is automatically queued and begins playback.

  Scenario: Jam recording prints performance into linear arrangement
    Given jam recording is armed during launcher playback
    When clips are launched across scenes
    Then linear arrangement clips are created matching the performance start and duration.
