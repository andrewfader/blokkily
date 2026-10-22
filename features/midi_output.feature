Feature: The song plays outboard gear and sends it MIDI clock
  A track can be played by a hardware synthesizer, drum machine or another
  program instead of an instrument of its own. The instrument panel's MIDI OUT
  row gives the selected track a MIDI output port and a channel. What the track
  plays then goes to that port on that channel: its notes, the controller
  movements of its patterns, and keys played into it. Each message is due when
  its sample reaches the speakers, so the gear sounds with the rest of the mix.
  The CLOCK row sends the song's MIDI clock to a port: Start when the song plays
  from its beginning, Song Position and Continue when it plays from anywhere
  else or jumps (a seek, the loop), a pulse every 24th of a beat on the song's
  tempo map, and Stop when it stops. An export sends nothing, because it renders
  faster than real time.

  The engine scenarios are proved by blokkily_midi_output_tests
  (tests/midi_output_tests.cpp, CTest midi_output_<case>). They read what a
  deterministic output received through the production render callback, each
  message with the engine frame it was played at. realtime_midi_output proves
  that sending allocates nothing. The interface scenario is proved by the
  bdd_midi_output gate (`blokkily --verify --scenario midi_output`), which saves
  build/artifacts/midi-output.png.

  Not done: microtonal retuning is not sent; parameter locks and automation
  have nothing to move on outboard gear. The MIDI clock setting belongs to the
  studio, not the song: the first window remembers it in the user's settings.

  # midi_output_notes
  Scenario: A track's notes are sent to outboard gear on its channel, to the sample
    Given a track played by a MIDI output on channel 3, and notes at samples 1000 and 3000
    When the song plays
    Then each note goes out on channel 3 on its own sample, at its velocity, and is let go on its last

  # midi_output_controllers
  Scenario: Controller movements go out on the track's channel
    Given the mod wheel moved to 64 at sample 2000 and the pitch wheel bent at 2500
    Then CC 1 = 64 and the bend go out on channel 3 on those samples

  # midi_output_live
  Scenario: Keys played into the track go out on its channel
    When a key is played on channel 1 of a keyboard routed to the track
    Then it goes out on the track's channel 3

  # midi_output_stop
  Scenario: Stopping the song lets go of every note it struck
    Given a long note sounding
    When the song stops
    Then its note-off goes out on the first sample after the stop

  # midi_output_clock, realtime_midi_output
  Scenario: MIDI clock follows the song
    Given clock sent to a port and a song at 120 BPM (a pulse every 1000 samples)
    When the song plays from its start
    Then Start goes out, then a pulse on every 1000th sample
    When it stops, Stop goes out
    When it plays again from sample 6000
    Then Song Position 1 (sixteenths) and Continue go out, then pulses from sample 6000

  # midi_output_bounce
  Scenario: An export sends nothing to outboard gear
    When the song is bounced
    Then nothing goes out, and the song is sent again when it next plays

  # midi_output_state
  Scenario: The track's channel is saved with the song
    Then the channel is the MIDI Out instrument's state, it survives a save and a load with its port
    And a MIDI Out instrument that goes away sends All Notes Off on its channel

  # bdd_midi_output
  Scenario: The MIDI OUT panel sends a track and the clock to a port
    Given the deterministic output listed in the rendered MIDI OUT panel
    When the producer picks it for track 0, picks channel 10, and picks it for CLOCK
    Then the strip reads "MIDI · Deterministic output · CH 10"
    And played for a beat, the output receives the pattern's notes on channel 10 a beat apart, Start, a pulse every 1000 samples from the first note, and Stop
    And undo gives the track its synth back

  # bdd_midi_output
  Scenario: The first window remembers where the clock goes and which keyboard plays
    Given a window that remembers the clock port in its settings
    When the producer picks OFF for CLOCK, and then the deterministic output
    Then the settings say no port, and then that port
    When a later launch finds the remembered port listed
    Then the clock goes to it again without being picked
    When the remembered port is not plugged in
    Then the clock stays off and the port stays remembered for when it is
    When the producer plays from the deterministic input, and then picks None
    Then the settings say that input, and then none
    When a later launch finds the remembered input listed
    Then it is opened again without being picked
    And the user's own settings file is never written by the gate
