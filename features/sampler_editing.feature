Feature: Editing a sampler: multi-samples, velocity layers and the waveform
  MULTI in the sampler panel imports a multi-sampled instrument from many
  files at once. Each file's key is read from its name (Piano_C#3.wav), else
  from the file itself, else the next key up from C3; its velocity layer
  from its name (v1, v2, ..., or pp, mf, ff). Each layer's samples split the
  keyboard between them; the layers split the velocities evenly, softest
  lowest. EDIT opens a map of every zone (keys across, velocities up) where a
  click picks the zone to edit, the zone's velocity range, and its file's
  waveform, where START, END and the loop's markers are dragged. The import
  and each edit are steps of history. The left rail scrolls when it is
  taller than the window, so nothing at its foot is pushed out of reach.

  # sampler_multisample_names
  Scenario: Keys and layers are read from file names
    Then Piano_C4 is 60, harp-F#2 is 42, Strings Db3 is 49, Pad A-1 is 9, Cs4 is 61
    And Bass_hit, Cello 120bpm, Abacus and Track 12 name no key
    And v2, vel10, pp, mf and ff name layers 2, 10, 2, 5 and 7

  # sampler_multisample_map
  Scenario: Roots split the keys and layers split the velocities
    Given roots 48, 60 and 72 in two layers
    Then 48 answers 0-54, 60 answers 55-66, 72 answers 67-127
    And the soft layer answers velocities 1-63, the loud 64-127
    And every key at every velocity is answered by exactly one zone

  # sampler_multisample_render
  Scenario: The sampler plays the layer and root the velocity and key choose
    Given a soft 1 kHz sample rooted on C4 and a loud 480 Hz loop rooted on C5
    Then key 66 at velocity 38 sounds 1414 Hz and at velocity 114 sounds 339 Hz

  # bdd_sampler_editing
  Scenario: In the window
    Given the sampler on a track
    When four files named Lead_C4_pp, Lead_C4_ff, Lead_C5_pp and Lead_C5_ff are imported
    Then the program has two roots in two velocity layers, and one undo takes the import back
    When EDIT is opened and the map's last box clicked
    Then Lead_C5_ff is the zone edited, its velocities 64-127, and editing them undoes alone
    And the waveform is drawn, START and END move on it, and loop points turn a loop on
    When the waveform is double-clicked twice in its middle
    Then it shows a quarter of the file around there, drawn from those frames, reads ×4.0, and each marker stands at its frame
    And two right double clicks show the whole file again, and the LOOP row's labels read whole
    And C5 struck through the production callback sounds
    And the browser keeps its room and the rail's foot can be scrolled to
