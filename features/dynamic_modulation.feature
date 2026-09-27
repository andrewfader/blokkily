Feature: Universal dynamic modulation engine modulates parameters in real time
  LFOs, envelope followers, and macro knobs modulate instrument and effect
  parameters at runtime. Modulators run on the audio thread without
  allocating or locking, and parameter modulation remains distinct from
  static parameter automation.

  Executable: tests/dynamic_modulation_tests.cpp (CTest dynamic_modulation_<case>);
  tests/realtime/dynamic_modulation.cpp (realtime_dynamic_modulation).

  # dynamic_modulation_lfo_shapes
  Scenario: LFO waveforms generate expected periodic trajectories
    Given an LFO configured with sine, triangle, saw, square, and sample-and-hold
    When the LFO is processed over full oscillation cycles
    Then each waveform reaches its mathematical bounds
    And tempo-sync matches the song tempo accurately

  # dynamic_modulation_envelope_follower
  Scenario: Envelope follower tracks input signal dynamics
    Given an envelope follower with configured attack and release times
    When audio bursts are processed through it
    Then the envelope rises during the burst according to attack
    And falls smoothly after silence according to release

  # dynamic_modulation_macro_routing
  Scenario: Macro knob modulates multiple parameters with range scaling
    Given a macro control assigned to multiple targets with min and max bounds
    When the macro value is moved from 0 to 1
    Then parameter modulation events are generated with accurately scaled values
    And base parameter automation is preserved untouched

  # dynamic_modulation_engine_dispatch
  Scenario: Dynamic modulations reach running processors in real time
    Given a song engine with an active modulation matrix
    When the engine processes audio chunks
    Then parameter modulation events reach instruments and insert effects
    And no allocations or locks occur on the audio callback
