Feature: Effects process tracks, returns and the master, in time with one another
  A track's instrument is followed by an insert chain; tracks send to return
  buses that have chains of their own; the master bus has one too. Effects
  are the built-ins (EQ, delay, reverb, compressor), CLAP and VST3 plugins,
  all behind the same processor boundary. Every latent effect is compensated
  so every path reaches the speakers aligned, live and in a bounce
  (decision 10). Bypass, send levels and return strips are mixer moves on the
  running engine; adding or removing an effect or a return rebuilds the graph
  and keeps every processor that did not change.

  Each scenario names the check that proves it: `effects_<case>` runs
  `blokkily_effects_tests <case>` against the real CLAP and VST3 effect
  fixtures built by the suite, reading rendered audio from the production
  SongEngine::process(); `realtime_effects` runs blokkily_realtime_checks; the
  `bdd_effects` gate drives the real application (`--scenario effects`) and
  its reached() labels are quoted.

  # effects_insert_chain
  Scenario: An insert chain processes the track in order
    Given a track playing a steady level of 1 into the CLAP effect, then the VST3 effect
    When the song plays
    Then nothing is heard for the chain's 96 samples of latency
    And then the track is heard at 0.25 x -0.25 = -0.0625 through its strip

  # effects_bypass_live
  Scenario: Bypass is live and keeps the chain's latency
    Given that chain playing an impulse every 4000 samples
    When the CLAP effect is bypassed and then brought back while the song plays
    Then no processor is swapped and nothing is prepared again
    And every impulse still comes out 96 samples late
    And while bypassed only the VST3's -0.25 is applied

  # effects_sends_and_returns
  Scenario: Sends feed a return post-fader and pre-pan, and follow their level
    Given a track panned hard left sending at 0 dB to a return
    Then the return is heard on both sides: the send is taken before pan
    When the send is turned down 6 dB, then the track's fader 6 dB
    Then the return halves, then halves again: a post-fader send follows the fader
    When the send is switched to pre-fader
    Then the fader no longer changes what the return hears
    When the track is muted
    Then neither its direct sound nor its send is heard
    When the return is muted instead
    Then only the track's direct sound is heard

  # effects_solo_safe_returns
  Scenario: Returns are solo-safe
    Given two tracks both sending to one return
    When one track is soloed
    Then the other track and its send fall silent
    And the return still carries the soloed track's send

  # effects_delay_compensation
  Scenario: Plugin delay compensation aligns every path
    Given a dry track sending to a return with the CLAP effect (64 samples),
      a track through the CLAP effect and a track through the VST3 effect (32)
    When the same impulse plays on all three
    Then the output latency is 64 + 64 = 128 samples
    And every path's impulse lands on the same output sample, summed

  # effects_master_insert
  Scenario: The master bus has inserts of its own
    Given the VST3 effect on the master bus
    Then the whole mix comes out 32 samples late, inverted, at -0.25
    When it is bypassed and the master gain lowered, live
    Then the mix passes at the master gain

  # effects_project_records
  Scenario: A saved project plays its effects the same after loading
    Given a song with inserts on a track, a return and the master, sends pre and post,
      a bypassed insert and a CLAP effect with its own saved gain
    When it is saved and loaded again
    Then the file round-trips byte for byte
    And the loaded song renders exactly what the saved one did
    And the CLAP effect plays at its saved gain

  # effects_bounce_trims_latency
  Scenario: A bounce drops the compensation and keeps the tail
    Given a track through the CLAP effect (64 samples) and the built-in delay
    When the song is bounced and the file read back
    Then the file is the song's length plus the delay's tail
    And it equals the live render with the first 64 samples dropped, sample for sample
    And the first impulse is at sample 0 of the file
    And the echoes after the song's end are in the file

  # effects_tempo_synced_delay
  Scenario: A tempo-synced delay follows a tempo change
    Given the built-in delay synced to half a beat, and a tempo map from 120 to 90 BPM
    When an impulse plays before the change and another after it
    Then the first echo comes 12000 samples later and the second 16000

  # effects_transport_per_chunk
  Scenario: Every processor is told where the song is before each block
    Given an insert on a track, a tempo change and a 7/8 meter
    When blocks play on either side of the tempo change, and one with the song stopped
    Then each block's transport carries the tempo, beat, bar and meter the maps give there
    And the stopped block says the song is stopped
    And an edit the insert reports is stamped with its own address and sample

  # effects_graph_signature
  Scenario: Inserts and returns are part of the graph; mixer moves are not
    Given a song with inserts on a track, a return and the master
    Then each insert is named at its processor address
    And bypass, sends, levels and state leave the graph as it was
    And adding or swapping an insert changes it

  # effects_insert_adoption
  Scenario: Rebuilding keeps each running effect, following its chain
    Given a track with an EQ then a delay, and another track with a reverb
    When the EQ is removed
    Then the delay moves up a slot as the same instance and nothing is created again
    When the first track is deleted
    Then the reverb follows its track as the same instance

  # effects_state_capture
  Scenario: What an effect was dialled to is saved on rebuild and on save
    Given a running CLAP effect turned to 0.75 by its own window
    When its state is captured into the song
    Then the song holds exactly what the effect saved
    And a slot that now names another plugin is left alone
    And after saving and loading, the effect is heard at 0.75

  # effects_scan_kind
  Scenario: The scanner tells instruments from effects
    Given the CLAP and VST3 synth fixtures and the CLAP and VST3 effect fixtures
    When each is scanned out of process
    Then the synths are instruments and the effects are effects
    And the kind survives the helper's answer and the scan cache (version 2)
    And a version 1 cache is scanned again rather than trusted

  # effects_builtin_catalog
  Scenario: The built-in effects are offered without a scan
    Then EQ Three, Delay, Reverb and Compressor are listed as effects
    And the processor factory creates each as a stereo effect
    And an unknown built-in is refused with a reason

  # realtime_effects
  Scenario: Effects keep the real-time rules
    Given CLAP, VST3 and built-in inserts on a track, a return and the master, with sends
    When a thousand blocks play through a tempo ramp, a seek and loop wraps,
      with bypass, send levels and pre/post moved between blocks
    Then no audio-thread call allocates or frees
    And the song and the return are heard

  # bdd_effects: "effects: the browser lists effects, built-ins included"
  Scenario: The browser lists instruments and effects apart
    Given the CLAP synth, the VST3 synth, a SoundFont and the CLAP effect are installed
    When the producer clicks FX above the plugin browser
    Then the browser lists the CLAP effect and the four built-ins, and no instrument

  # bdd_effects: "effects: an inserted effect is heard"
  Scenario: An effect clicked in the browser is inserted after the track's instrument
    Given the CLAP synth on the selected track, heard at 0.25
    When the producer clicks the CLAP effect in the browser
    Then the rack lists it, the synth keeps running as the same instance
    And the track is heard at 0.0625 with 64 samples of latency

  # bdd_effects: "effects: bypass is live and undoable"
  Scenario: Bypass from the rack is live and undoable
    When the producer clicks BYP on the insert
    Then the track is heard at 0.25 again with no rebuild
    And undo brings the effect back, still without a rebuild

  # bdd_effects: "effects: a send feeds a return live"
  Scenario: A return fed by a send
    When the producer adds a return and drags the track's SEND A up
    Then the return strip and the send are on screen at a usable size
    And the track is heard through the return as well, with no rebuild

  # bdd_effects: "effects: the master bus takes an insert"
  Scenario: The master bus takes an insert from the rack
    When the producer clicks FX on the master strip and then EQ Three in the browser
    Then the master rack lists the EQ and the level is unchanged

  # bdd_effects: "effects: a rebuild keeps and saves what an effect was dialled to"
  # bdd_effects: "effects: a saved project plays its effects again"
  Scenario: An effect's settings survive a rebuild and a save
    Given the CLAP effect turned to 0.5 while it runs
    When a track is added, which rebuilds the graph
    Then the effect is the same instance and the song holds its new state
    When the project is saved and opened again
    Then the effects, the return, the send and the master insert are restored
    And the track is heard exactly as before

  # bdd_effects: "effects: the bounce is trimmed of the compensation"
  Scenario: The bounce starts where the song does
    When the producer exports the song
    Then its first sound is on its first sample, not 64 samples in
    And it runs the song's length plus the tail

  # wave2_bounce_after_playback (regression)
  Scenario: An export after playback renders the song from silence
    Given a song whose synth runs through the CLAP effect, the VST3 effect and a delay,
    And a send into a reverb return and a compressor on the master
    And the song has played for 7000 frames, so echoes, tails and compensation lines are full
    When it is exported
    Then the file read back is sample for sample the export of a freshly prepared engine

  # wave2_bounce_resumes_clean (regression)
  Scenario: Playback after an export resumes from silence where it was
    Given the same song playing, 5120 samples in
    When it is exported
    Then the transport is playing again at sample 5120
    And what it plays from there is exactly what a fresh engine plays from sample 5120

  # wave2_serve_every_processor; bdd_effects "effects: an insert's main-thread callback is served" (regression)
  Scenario: Every insert is served on the main thread
    Given the CLAP effect on a track, a return and the master, each asking for a main-thread callback
    When the application's plugin service runs once
    Then each effect's on_main_thread has run exactly once
    And in the real application the effect on track 0 is served both directly and by the timer
