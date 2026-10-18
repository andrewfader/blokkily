Feature: Exchange the song with another DAW as a DAWproject
  DAWPROJ OUT on the rail writes the song as a .dawproject, the open exchange
  format (github.com/bitwig/dawproject, 1.0) that Bitwig Studio, Studio One,
  Cubase and others open. It holds what the engine plays: each track's
  notes, controller movements, per-note expression and value step locks as
  Song::arrange compiles them for the render callback, a clip per placement
  of a part, named after its section. With them go the mixer (volume, pan,
  mute, solo, sends, returns, the master), the tempo map and the meters, the
  automation lanes, the audio clips with their files, and every running
  plugin's state as the other host loads it: a CLAP plugin's state stream, a
  VST3's .vstpreset under its class id. What the format has no place for is
  named in the export's status, never dropped in silence.
  DAWPROJ IN opens a .dawproject - another host's, or Blokkily's own - as a
  new, unsaved session: the tempo and meter maps, each track's clips as
  sections (clips of one name and length share a section, a part per track)
  and placements, notes with velocity, retune and expression, controllers,
  the instruments and effects found among the installed plugins (CLAP by id,
  VST3 by name, with their states), Blokkily's own devices from their
  placeholders, the mixer, returns and sends, audio clips unpacked beside the
  session, and the strip and parameter automation. What it cannot bring in
  is named in the session's status.

  Proved by blokkily_dawproject_tests (tests/dawproject_tests.cpp, CTest
  dawproject_<case>) and by the bdd_dawproject gate (`blokkily --verify
  --scenario dawproject`, src/app/verify/scenario_dawproject.cpp), which
  saves build/artifacts/dawproject.png.

  Not done: the scene launcher's scenes; clip gain
  and an audio clip's pitch shift; the arpeggiator (the notes are written as
  played into it); modulation step locks, LFOs, macros and followers;
  sidechain keys and instrument outputs routed to other tracks; SoundFont,
  sampler, LV2 and built-in processors, which go as placeholders other hosts
  cannot load.

  # dawproject_notes
  Scenario: The notes are those the engine plays, clip by clip
    Given two CLAP tracks playing one section placed twice
    And notes off the grid, a probability, a ratchet, a retuned note with pitch and timbre expression, a controller and value step locks
    When the song is exported as a DAWproject
    Then each track has a clip per placement, named after its section
    And every note read back starts, lasts and sounds as Song::arrange plays it
    And the retune and the pitch expression are one transpose lane in semitones, the timbre a lane of its own
    And the controller is a channelController lane, the pitch wheel a pitchBend lane
    And the value locks are hold points on the instrument's listed parameter
    And the tempo ramp is a linear tempo point, the song's notes the file's comment

  # dawproject_render
  Scenario: A song rebuilt from the file alone sounds like the song
    Given a song with a muted track, a send into a return, and a stepped gain lane
    When it is exported, and a song is rebuilt from nothing but the file
    Then the rebuilt song bounces sample for sample as the original does

  # dawproject_clap_state
  Scenario: A CLAP plugin's state goes as its state stream
    Given the CLAP fixture with its level moved
    When the song is exported
    Then its ClapPlugin device names the plugin's id and holds the state stream byte for byte
    And a fresh instance loaded with it plays what the original plays

  # dawproject_vst3_state
  Scenario: A VST3 plugin's state goes as a .vstpreset under its class id
    Given the VST3 fixture with its level moved, and an automation lane on it
    When the song is exported
    Then its Vst3Plugin device names the 32-digit class id and holds a .vstpreset
    And the automated parameter is listed by its VST3 ParamID with its lane
    And a fresh instance loaded with the preset holds the moved level

  # dawproject_audio_clip
  Scenario: An audio clip goes with its file and lands where it plays
    Given an audio clip trimmed at its start, across a tempo change
    When the song is exported
    Then the archive holds the file byte for byte
    And the clip starts and lasts as it sounds, its fade in seconds
    And each warp marker puts its audio where the engine plays it

  # dawproject_omissions
  Scenario: What the format cannot carry is named
    Given a SoundFont track with its arpeggiator on, a CLAP track whose state was not captured, a modulation lock, an instrument output and an LFO
    When the song is exported
    Then the report names each of them
    And the SoundFont goes as a placeholder device named after its file

  # dawproject_schema
  Scenario: The archive is a plain ZIP the official schema accepts
    Given a song exported twice
    Then another tool unpacks it, and project.xml and metadata.xml validate against the official schemas
    And the two exports are byte for byte the same, with no partial file left
    And a song with an audio clip, a built-in effect and a 7/8 bar validates too

  # bdd_dawproject
  Scenario: The rail's DAWPROJ chips export the song the window plays, and open it again
    Given the real window with two CLAP tracks and a return
    Then DAWPROJ IN and DAWPROJ OUT sit under MIDI IN and MIDI OUT, their labels whole, and they keep their usable size
    When the song is exported
    Then the status names the file, two tracks, sixteen notes and two plugin states
    And the file reads back with the engine's notes and each running instrument's state
    When the file is opened again in the window
    Then nothing is left out, every track's instrument state, strip, sends and notes are the song's, and it bounces sample for sample as the song did

  # dawproject_import_round_trip
  Scenario: A song exported and opened again is the same song
    Given the rich song, with a meter change, built-in effects with their settings, a return and the lead's strip automated
    When it is exported and the file opened again with the CLAP fixture installed
    Then the tempo and meter maps, every track's strip, sends, instrument state, notes and controllers, the effects, the return, the master and the automation are what they were
    And the song made from the file alone bounces sample for sample as the original

  # dawproject_import_placeholders
  Scenario: Blokkily's own instruments come back from their placeholders
    Then a SoundFont and an LV2 instrument written as placeholders open as the same slots, state included

  # dawproject_import_foreign
  Scenario: A song another DAW wrote opens and plays
    Given a project.xml as another host lays it out, zipped by hand: 128 BPM in 3/4, a CLAP instrument by id, a clip of notes and a controller lane at beat 6
    Then it opens with its tempo, meter, track, pan, master, the instrument found among the installed plugins, the clip as a section and its notes and controller
    And its first note is heard at frame 135000, beat 6 at 128 BPM

