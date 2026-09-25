Feature: Built-in effects and effect plugins shape the sound they are given
  Blokkily's own EQ, delay, reverb and compressor process a track's audio in
  place behind the same PluginInstance boundary as CLAP and VST3 plugins, and
  the suite builds a real CLAP effect and a real VST3 effect whose work can be
  read off the rendered audio. Each scenario is proved from rendered samples
  by the CTest test named above it (blokkily_builtin_effects_tests <case>, or
  blokkily_realtime_checks). Insert chains, returns and the browser that
  hosts them are features/effects.feature (item 2.4).

  # builtin_eq3_response
  Scenario: The three-band EQ boosts and cuts where it is told to
    Given the built-in EQ at its defaults
    Then a 1 kHz tone passes within 0.01 dB
    When the low shelf is set to +6 dB at 200 Hz
    Then a 40 Hz tone comes out 6 dB louder, within 1 dB, and 8 kHz is untouched
    When the mid band is set to -6 dB at 1 kHz
    Then a 1 kHz tone comes out 6 dB quieter, within 1 dB, and 50 Hz and 15 kHz are untouched
    When the high shelf is set to +9 dB at 4 kHz
    Then a 16 kHz tone comes out 9 dB louder, within 1 dB, and 100 Hz is untouched

  # builtin_delay_echoes
  Scenario: The delay repeats a sound at its time, each echo half the last
    Given the built-in delay at 250 ms, feedback 0.5, mix 0.5
    When a single impulse is played through it
    Then the dry impulse sounds at 0.5
    And echoes arrive at exactly 250 ms and 500 ms, at 0.5 and 0.25, a ratio of 0.5
    And nothing sounds between the echoes
    And past the tail it reports the echoes are 80 dB down

  # builtin_delay_tempo_sync
  Scenario: A synced delay follows the song's tempo
    Given the built-in delay synced to half a beat
    When the transport reports 120 BPM
    Then the first echo arrives at 250 ms
    When the transport reports 90 BPM instead
    Then the first echo arrives at 333 ms
    And an unsynced delay ignores the tempo

  # builtin_reverb_tail
  Scenario: The reverb leaves a decaying tail
    Given the built-in reverb at its defaults
    When a single impulse is played through it
    Then after the dry sound a tail sounds in both channels, differently in each
    And the tail is quieter at one second than at the start, and quieter again at two
    And by the tail length it reports it has fallen 80 dB

  # builtin_compressor_level
  Scenario: The compressor turns a loud tone down by its ratio
    Given the built-in compressor at threshold -20 dBFS and ratio 4:1
    When a -6 dBFS tone is played through it
    Then it settles at about -16.5 dBFS
    And a tone under the threshold passes unchanged
    And 6 dB of makeup adds 6 dB, and a 10:1 ratio settles at about -18.6 dBFS

  # builtin_determinism
  Scenario: A built-in effect renders the same audio every time
    Given the same input and the same parameter moves at the same samples
    When each built-in renders it twice in 256-frame blocks and once in 97-frame blocks
    Then all three renders are bit-identical
    And re-activating an effect forgets its signal and its modulation

  # builtin_interface
  Scenario: Built-in effects are ordinary processors
    Given the built-in effects EQ, delay, reverb and compressor
    Then each takes stereo audio and no notes, adds no latency, and lists its parameters
    And a parameter value lands at its sample offset and modulation adds to it, clamped
    And a saved state loads into a new instance that renders exactly the same
    And a garbage or truncated state is refused and changes nothing

  # builtin_clap_effect
  Scenario: The CLAP effect fixture processes the audio it is given
    Given the CLAP effect loaded through its official clap_entry
    Then it takes stereo audio and no notes and lists Gain, default 0.25
    And it reports 64 samples of latency and of tail
    When a steady signal plays through it
    Then it comes out 64 samples late at 0.25
    And a gain change and a modulation are heard from their sample offsets
    And its saved gain is heard again after loading into a new instance

  # builtin_vst3_effect
  Scenario: The VST3 effect fixture processes the audio it is given
    Given the VST3 effect bundle built by the suite and hosted through JUCE
    Then it takes stereo audio and no notes and lists Gain, default 0.25
    And it reports 32 samples of latency and of tail
    When a steady signal plays through it
    Then it comes out 32 samples late, inverted, at -0.25
    And a gain change is heard from its sample offset
    And its saved gain is heard again after loading into a new instance

  # builtin_fixture_chain
  Scenario: Effects chain in place
    Given the CLAP effect followed by the VST3 effect on one block
    When a steady signal of 1 plays through both
    Then it comes out 96 samples late at -0.0625

  # realtime_builtin_effects
  Scenario: Built-in effects keep the real-time rules
    Given every built-in effect and both effect fixtures
    When a thousand blocks are processed with parameter moves, modulation and tempo changes
    Then no audio-thread call allocates or frees
    And each still sounds and has changed the tone it was given
