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
  they play. The piano roll's controller lane shows one controller at a
  time, chosen from its picker (BEND, MOD, EXPR, any numbered CC, PRESSURE),
  and edits it: a drag draws a stroke, a drag from a point moves it, the
  right button erases; each gesture is one step of history in the canonical
  pattern, and the running engine plays it without a rebuild.

  MPE: with the MIDI IN panel's MPE chip on (the lower zone, member channels
  2-16, per-note pitch range 48 semitones), or turned on by the keyboard's
  MPE Configuration Message (RPN 6), a member channel's pitch bend, CC 74 and
  channel pressure (and poly pressure there) become per-note expression of
  the note it holds (PluginEvent::note_expression), and that note is marked
  expressive. It is recorded with the note (NoteExpression on the trigger,
  per voice, at its offset into the note; kept on its voice when a take
  makes a chord), saved as additive `express` records, played at its
  sample by the arrangement, the launcher and an export. What each target
  hears:
    CLAP: CLAP_EVENT_NOTE_EXPRESSION tuning (retune plus bend), brightness
    and pressure, addressed by key - nothing degrades.
    SoundFont: the note gets a FluidSynth channel of its own with a 48
    semitone bend range; pitch is exact, timbre is CC 74 and pressure is
    channel pressure on that channel, which the SF2 default modulators turn
    into nothing and vibrato depth respectively unless the SoundFont maps
    them. At most 15 expressive or retuned notes sound at once; the oldest
    gives its channel up.
    VST3: the note gets a MIDI voice channel of its own (2-16) through the
    JUCE host; pitch is its pitch bend within the two semitones the adapter
    announces (a larger per-note bend is clamped there), timbre CC 74 (which
    stays on the channel until changed), pressure channel pressure. No
    VST3 note-expression events are sent.
    Sampler: per-note pitch only; timbre and pressure are ignored.
  Master-channel messages, and other CCs on a member channel, still reach
  the whole instrument.

  Not done: Controller movements are stamped at the block they arrived in, as
  notes are. Per-note expression is not drawn or edited in any editor (it
  travels with its step when the step is moved, copied or transposed). No
  upper-zone MPE chip (the keyboard's configuration message sets it). The built-in sampler ignores poly pressure; the SoundFont and VST3
  paths send it on the channel holding its key.

  Executable: tests/continuous_midi_tests.cpp (CTest continuous_midi_<case>),
  tests/record_tests.cpp (record_sustain_pedal),
  tests/realtime/continuous_midi.cpp (realtime_continuous_midi), the
  bdd_midi_recording gate (src/app/verify/scenario_midi.cpp) and the
  bdd_controller_lanes gate (src/app/verify/scenario_controllers.cpp).

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
    And the piano roll's controller lane counts each one

  # bdd_controller_lanes
  Scenario: The controller lane is drawn, moved and erased with the mouse and heard on its sample
    Given the real application with the CLAP fixture playing a long A4 in its tone mode
    And the piano roll's controller lane at a usable size
    When BEND is picked from the lane's picker and a stroke is dragged along the top from tick 480 to 960
    Then the pattern holds a full bend every 15 ticks from 480 to 960, and one undo takes the whole stroke back
    And the steps, the tracker and the roll show the pattern as before
    When the last point is dragged to tick 1200 at the centre and the first is right-clicked
    Then the point moves and the first is erased, with no rebuild of the engine
    And the song exported through the running engine is the CLAP fixture given that bend on its tick's sample, and differs from it a sample early or late
    When MOD is picked and the lane clicked half way up, and CC with its number raised to 8 is picked and the lane clicked at the top
    Then CC 1 holds 64 and CC 8 holds 127 on their ticks, and the bend is untouched

  # continuous_midi_mpe_clap_live
  Scenario: An MPE keyboard plays each note's own expression into CLAP and records it
    Given the CLAP fixture and a keyboard that sends the MPE Configuration Message for 15 members
    When channel 2 sends a +12 bend and full pressure, then a note, then CC 74, and channel 3 a second note
    Then the first note is heard louder by its own pressure and brighter by its own timbre, the second untouched
    And CLAP is sent tuning, pressure and brightness note expressions for key 69 alone
    And the take keeps the first note's bend, pressure, timbre and later bend at their offsets, and writes them onto its step

  # continuous_midi_mpe_clap_playback
  Scenario: Recorded per-note expression plays back to CLAP as note expressions, on its sample
    Given a note with pressure 1.0 at 250 ticks and +12 semitones at 500
    When the song plays through the CLAP fixture
    Then the level doubles on sample 12500 exactly, and the note expressions arrive at their block offsets
    And in a chord only the voice that carries the pressure is pressed
    And a launched loop of the pattern plays it sample for sample as the arrangement does, and the export reads back equal

  # continuous_midi_mpe_soundfont
  Scenario: A SoundFont bends one note of a chord through its own channel
    Given an A4-E5 chord on a real SF2 whose A4 carries a +2 semitone per-note bend
    Then the A4 sounds at 493.9 Hz while the E5 sounds where it does without the bend

  # continuous_midi_mpe_vst3
  Scenario: A VST3 instrument hears per-note expression on the note's own channel
    Given the suite-built VST3 fixture through the production JUCE host
    When a note carries +1 semitone, or +7, or pressure 1.0 at tick 400
    Then it sounds a semitone up; the +7 is clamped to the channel's two semitones; the pressure doubles its level

  # continuous_midi_mpe_records
  Scenario: Per-note expression is saved with its note
    When a song whose note carries expression is saved
    Then each value is an express record, read back byte for byte, and an older file loads with none
    And malformed express records (a missing voice, trigger or pattern, a bad kind, offset or value) are refused
    And a take that merges two expressive notes into a chord keeps each voice's expression on its voice

  # bdd_midi_recording: "midi: an MPE note is recorded with its own bend and pressure"
  Scenario: MPE is switched on in the application and an MPE note is recorded
    Given the real application with an armed song and the deterministic keyboard
    When the MPE chip is clicked on the MIDI IN panel and a note on channel 2 is bent and pressed while the song plays
    Then the pattern holds the note with its own expression, and the chip turns MPE off again

  # realtime_continuous_midi
  Scenario: Playing and chasing controllers never allocates on the audio thread
    Given a pattern full of wheel, mod-wheel and poly pressure movements on the CLAP fixture
    And a note carrying pitch, pressure and timbre expression
    And a keyboard bending and pressing on the same track, and MPE notes struck, bent, pressed and let go
    When SongEngine::process runs 1000 times across loop wraps and seeks
    Then no allocation happens inside it
