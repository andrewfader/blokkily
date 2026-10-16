Feature: A scene can carry its own tempo
  Right-clicking a scene's chip in the LAUNCH view shows the scene's row:
  TEMPO with - and +, which start from the tempo playing, and SONG, which
  gives the scene the song's own tempo again. A scene with a tempo shows it
  on its chip. Launching the whole scene plays at its tempo: the engine's
  tempo override changes, as it does when an external clock is followed, and
  the transport counts at it. The song's tempo map is never edited and
  launching is not an edit. The tempo lasts until STOP ALL or the transport
  stops. A scene without a tempo keeps whatever tempo is playing. A followed
  MIDI clock wins over a scene's tempo. An export, a freeze or any other
  bounce renders the song at its own tempo map. The tempo is saved on the
  scene's record (0 or none: the song's), and a tempo outside 20..300 BPM is
  refused.

  Proved by the bdd_scene_tempo gate (`blokkily --verify --scenario
  scene_tempo`, src/app/verify/scenario_scene_tempo.cpp), through the
  production render callback, which saves build/artifacts/scene-tempo.png.

  # bdd_scene_tempo
  Scenario: A scene launched at its own tempo
    Given the CLAP synth playing a key for the first half of each 4/4 bar, the song at 120 BPM, as the arrangement and as SCENE 1's cell
    When the scene's row is opened from its chip and + and - are clicked
    Then the tempo goes to 121 and back to 120 from SONG
    And given 150 BPM the row shows 150.0 in full and the chip says so, its label not cut short
    When the scene is launched from its chip
    Then the transport counts at 150 BPM and the song is placed at it, without its tempo map being edited or the song made dirty
    And the bars are heard 76800 samples apart, not the 96000 of the song's own tempo
    And an export made while it plays is, sample for sample, the export made at the song's own tempo
    When STOP ALL is clicked
    Then the song plays at 120 BPM again
    And saved and loaded the scene keeps 150 BPM, a scene at 5 BPM is refused, and SONG takes it off as one undo step
