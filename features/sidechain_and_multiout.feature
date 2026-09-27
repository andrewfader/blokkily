Feature: Inter-track sidechaining and multi-output routing
  Sidechain compressor inserts tap audio from other tracks pre-fader and duck
  audio accordingly. Multi-output instruments route auxiliary audio buses
  into discrete mixer tracks. Tracks are automatically topologically sorted
  to ensure dependencies are rendered in correct order without allocations
  or locks on the audio thread.

  Executable: tests/sidechain_tests.cpp (CTest sidechain_<case>);
  tests/realtime/sidechain.cpp (realtime_sidechain).

  # sidechain_compression
  Scenario: Sidechain compressor ducks track signal when key input triggers
    Given a compressor insert on a synth track configured to sidechain from a kick track
    When the kick track sounds a loud burst
    Then the synth track signal is attenuated according to compressor ratio
    And when the kick track is silent the synth track passes unattenuated

  # sidechain_topological_sort
  Scenario: Tracks with sidechain dependencies are topologically sorted
    Given multiple tracks with dependency routes
    When the render order is computed
    Then source tracks are placed before dependent consumer tracks
    And cyclic dependencies are safely broken

  # sidechain_multi_output
  Scenario: Multi-output instrument routes auxiliary audio to destination track
    Given a source track generating audio and an auxiliary multi-output route to another track
    When the engine processes audio chunks
    Then the destination track receives and mixes the auxiliary audio
