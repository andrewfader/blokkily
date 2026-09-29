Feature: Plugin sidechain inputs and multi-output instruments
  A CLAP or VST3 insert that declares a sidechain input (a CLAP input port
  that is not the main one, a VST3 aux input bus) is fed the key its
  EffectSlot::sidechain names: the key track after its inserts and before its
  fader, exactly as the built-in compressor hears it. An instrument that
  declares extra outputs (CLAP output ports beyond the main one, VST3 aux
  output buses) can break each out to a mixer channel of its own: a track whose
  Track::source names the instrument's track and output, rendered after it,
  with its own fader, pan, mute, solo, inserts and sends. The output reaches
  the mix through that channel alone, never also through the source track.
  An aux output is the instrument's own sound, so soloing the source track
  solos its channels with it (Song::soloed); soloing a channel alone does
  not solo its source.

  The fixtures are built by the suite and loaded through the production
  adapters: the CLAP effect scales its input by (1 - key level), the VST3
  effect likewise; the CLAP and VST3 synths put minus half of their main
  output on their aux output.

  Executable: tests/plugin_routing_tests.cpp (CTest sidechain_<case> and
  multiout_<case>), tests/realtime/plugin_routing.cpp (realtime_plugin_routing),
  and the bdd_plugin_routing gate (src/app/verify/scenario_routing.cpp).

  # sidechain_ports_declared
  Scenario: The hosts see the fixtures' extra ports
    Given the CLAP and VST3 effect fixtures and the CLAP and VST3 synth fixtures
    When they are loaded through the production adapters
    Then each effect reports a stereo sidechain input
    And each synth reports one aux output and no input

  # sidechain_clap_key
  Scenario: A CLAP effect is keyed on its sidechain port, post-insert and pre-fader
    Given the CLAP effect on track 0 keyed from track 1, which is muted, at -60 dB and runs the VST3 effect
    When track 1 plays 0.25
    Then track 0 is ducked by the key's level after track 1's insert (0.0625), not before it (0.25)
    And with the key silent, or no key named, track 0 is not ducked

  # sidechain_vst3_key
  Scenario: A VST3 effect is keyed on its aux input bus
    Given the VST3 effect on track 0 keyed from track 1, which is muted, faded and runs the CLAP effect
    When track 1 plays 0.25
    Then track 0 is ducked by the key's post-insert level
    And with the key silent, or no key named, track 0 is not ducked

  # sidechain_plugin_key_bounce
  Scenario: An export of plugin-keyed inserts is ducked as playback was
    Given a CLAP-keyed track hard right and a VST3-keyed track hard left, keyed from a muted track of bursts
    When the song is exported and read back
    Then every sample equals the live render
    And each side is halved while a burst plays and whole between bursts

  # multiout_clap_multi_out
  Scenario: A CLAP synth's aux output plays through its own channel only
    Given the CLAP synth on track 0 hard left and its aux output on track 1 hard right
    When a note plays
    Then the left is the main output alone (0.25, not 0.125) and the right the aux output alone (-0.125)
    And the channel's fader and mute move the aux signal alone, live, without replacing the instrument
    And muting the source leaves the channel sounding
    And with no channel the aux output is not heard at all, and a recompile routes it again

  # multiout_vst3_multi_out
  Scenario: A VST3 synth's aux output bus plays through its own channel only
    Given the VST3 synth on track 0 hard left and its aux output bus on track 1 hard right
    When a note plays
    Then the left is the main output alone and the right the aux output alone
    And the channel's fader and mute move the aux signal alone, live

  # multiout_multi_out_order
  Scenario: A channel renders after its source wherever it sits in the song
    Given the channel on track 0 and its source on track 1
    When the first block plays a note that starts on its first sample
    Then the channel hears that block's aux output

  # multiout_multi_out_bounce
  Scenario: An export of a multi-output song matches playback
    Given a CLAP or a VST3 synth whose aux channel runs the CLAP effect and sends to a return
    When the song is exported and read back
    Then every sample equals the live render
    And the main output is on the left and the processed aux channel with its return on the right

  # multiout_multi_out_song_model
  Scenario: The song keeps instrument outputs it can play, and saves them
    Given a song with an instrument output broken out to a channel
    When the output names a missing track, its own track, output 0, a channel with an instrument, a second channel, or a loop with a key
    Then the song is refused
    And removing a track re-indexes the source, removing the source leaves a plain track
    And the channel saves as an auxsource record, a file without it loads, a dangling one is refused

  # multiout_multi_out_solo
  Scenario: Soloing an instrument keeps its broken-out outputs audible
    Given a CLAP synth whose aux output has its own channel, and a third track playing
    When the synth's track is soloed
    Then the master bus plays the synth's main output and its aux channel, and not the third track
    And soloing the aux channel alone plays that channel without its source's main output
    And a muted aux channel stays muted under its source's solo
    And an export of the soloed song hears the same

  # realtime_plugin_routing
  Scenario: Plugin keys and instrument outputs keep the real-time rules
    Given the CLAP and VST3 synths' aux outputs on channels that run keyed CLAP and VST3 effects
    When a thousand blocks play across a seek and a recompile that unroutes an output and moves a key
    Then the render callback never allocates or frees, and both channels played their ducked aux outputs

  # bdd_plugin_routing
  Scenario: A producer keys a plugin and breaks out an instrument output
    Given the application with the CLAP synth on PAD and on a muted, faded KICK
    When the CLAP effect is inserted from the browser and its rack SIDECHAIN chip set to KICK
    Then the PAD is ducked while a key is held on the KICK, without a rebuild
    When the PAD strip's + OUT chip is clicked and AUX 1 chosen
    Then a PAD AUX 1 channel plays the aux output apart from the PAD, and its mute and fader move it alone, live
    And the channel undoes, is saved and loaded, and the export has the main output and the channel apart
