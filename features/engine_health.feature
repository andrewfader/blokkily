Feature: The window says how hard the audio device is pushed
  The mixer's header reads DSP n%: the share of each audio block's time the
  mix takes to render through the production callback, held at its recent
  worst so a spike is seen. Amber past 70%, red past 90% or once the device
  has dropped out; the dropouts the device reports are counted beside it, and
  the first is said in a notice that suggests a larger buffer.

  # bdd_pattern_swing
  Scenario: The load of the blocks just played is shown
    Given the song plays through the deterministic device's render callback
    When some blocks have been rendered
    Then the mixer's header reads DSP and a load above nothing, readable in full
