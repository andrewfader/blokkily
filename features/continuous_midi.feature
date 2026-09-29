Feature: Continuous MIDI: the wheel, controllers, pressure and the sustain pedal
  A track is one instrument, so a controller reaches every note it plays,
  whatever MIDI channel the keyboard sent it on. SoundFonts apply the wheel,
  control changes and channel pressure to the shared channel and to every
  retuned voice channel, and poly pressure to the channel holding its key.
  VST3 instruments hear them on channel one and on each retuned voice's
  channel, where the wheel is added to the note's own retune bend. CLAP
  instruments hear them on the channel notes are sent on. The built-in
  sampler follows the wheel (two semitones) and a CC 64 that reaches it.

  The sustain pedal of a MIDI keyboard is handled at the input: while it is
  down, a key's release is held back until the pedal comes up, for every
  instrument alike, and a take records how long each note was heard.

  Pitch bend, control changes (other than 64 and the channel-mode messages),
  channel pressure and poly pressure played into a recording song are
  written into the canonical pattern as controller movements
  (ContinuousEvent; poly pressure names the key the song's tuning sent the
  instrument), saved as `control` records, played at their sample, chased
  when the playhead jumps (poly pressure per key), and exported exactly as
  they play. The piano roll marks them in a
  read-only strip; they are not edited in any editor yet.

  Not done: MPE (per-note pitch, pressure and timbre) is not supported; a
  keyboard's per-channel controllers are merged onto the one instrument.
  Controller movements are stamped at the block they arrived in, as notes
  are. The built-in sampler ignores poly pressure; the SoundFont and VST3
  paths send it on the channel holding its key.

  Executable: tests/continuous_midi_tests.cpp (CTest continuous_midi_<case>),
  tests/record_tests.cpp (record_sustain_pedal),
  tests/realtime/continuous_midi.cpp (realtime_continuous_midi) and the
  bdd_midi_recording gate (src/app/verify/scenario_midi.cpp).

  # continuous_midi_soundfont_bend
  Scenario: A keyboard on any channel bends the SoundFont's notes
    Given a SoundFont track loaded from a real SF2 in FluidSynth
    And a note played on MIDI channel 2
    When the wheel is pushed all the way up on channel 2
    Then the rendered note is two semitones higher
    And a microtonal note on a retuned channel is bent by the wheel as well

  # continuous_midi_soundfont_sustain
  Scenario: The sustain pedal holds SoundFont notes until it is released
    Given a SoundFont track played from a MIDI keyboard
    When a key is released while the pedal is down
    Then the note is still sounding half a second later, far louder than without the pedal
    And once the pedal comes up the note decays

  # record_sustain_pedal
  Scenario: The sustain pedal holds CLAP notes until it is released
    Given the CLAP fixture played from a MIDI keyboard
    When a key is released while the pedal is down
    Then it keeps sounding until the pedal comes up, and then is silent

  # continuous_midi_vst3_bend
  Scenario: The pitch wheel reaches a VST3 instrument through the JUCE adapter
    Given the suite-built VST3 bundle loaded through the production JUCE host
    When a keyboard on channel 3 pushes the wheel up
    Then the rendered note is two semitones higher
    And a retuned note hears the wheel added to its own bend

  # continuous_midi_sampler_bend_sustain
  Scenario: The sampler follows the pitch wheel and the sustain pedal
    Given a sampler zone playing a looped A4
    When the wheel is pushed up
    Then the rendered note is two semitones higher
    And a CC 64 holds the note past its note-off until it is released

  # continuous_midi_pattern_sample_accurate
  Scenario: A recorded bend plays back on its exact sample
    Given a pattern holding a bend at tick 250 under a long note
    When the song plays through the CLAP fixture
    Then the render equals the plugin given the bend at sample 12500
    And differs from the bend one sample earlier or later

  # continuous_midi_chase
  Scenario: Controllers are chased when the playhead jumps
    Given a pattern whose wheel goes up at tick 100, with notes before and after it
    When the song wraps back to its start
    Then the note before the movement sounds with the wheel at rest
    And after a locate past the movement the next note sounds bent

  # continuous_midi_record_take
  Scenario: Controllers played while recording are written into the pattern
    Given an armed, running song and a keyboard on channel 4
    When the wheel, the mod wheel, the pedal and pressure are played
    Then the wheel, the mod wheel and pressure are captured and the pedal is not
    And they are written into the pattern where they were heard
    And they survive a save and load byte for byte, while an older file loads with none
    And malformed or out-of-range control records and movements are refused

  # continuous_midi_export_matches
  Scenario: An export plays the controller movements the engine plays
    Given a pattern with bends and a control change on a VST3 track
    When the song is bounced
    Then the file reads back equal to the live render, sample for sample

  # continuous_midi_poly_pressure
  Scenario: Poly pressure is recorded, played at its sample, chased and exported
    Given the CLAP fixture, whose pressed key sounds louder by its poly pressure
    And a keyboard whose key 60 the song's tuning sends to key 62
    When key 60 is held and pressed while an armed song runs
    Then the pressure is heard live and captured on key 62, and written into the pattern
    And a pattern's pressure lands on its exact sample (tick 250, sample 12500)
    And a note after the movement is pressed, a wrap chases the key back to rest and a locate past the movement chases it again
    And the export reads back equal to the live render
    And it is saved as "control 0 250 poly 62 127", read back byte for byte, an older file loads with none and malformed poly records are refused

  # bdd_midi_recording
  Scenario: The wheel is recorded in the application and marked in the piano roll
    Given the real application with an armed song
    When the wheel is moved on the MIDI keyboard while the song plays
    Then the pattern under the playhead holds the movements
    And the piano roll's controller strip marks each one

  # realtime_continuous_midi
  Scenario: Playing and chasing controllers never allocates on the audio thread
    Given a pattern full of wheel and mod-wheel movements on the CLAP fixture
    And a keyboard bending and pressing on the same track
    When SongEngine::process runs 1000 times across loop wraps and seeks
    Then no allocation happens inside it
