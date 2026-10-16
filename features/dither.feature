Feature: Dither on 16- and 24-bit exports
  When a bounce or a stem is written as a 16- or 24-bit WAV, TPDF dither
  (triangular noise of plus or minus one step) is added before each sample
  is rounded, unless DITHER in the export heading is turned off. A fade or
  a reverb tail quieter than a step then stays a sound with a faint hiss
  under it instead of breaking up into distortion or silence. The noise
  comes from a generator seeded alike for every file, so the same mix
  still writes the same bytes. Float files, MP3 and Ogg are never dithered.

  # export_formats_dither_quiet
  Scenario: A tone quieter than a step survives
    Given a 1 kHz tone a third of a 16-bit step high
    When it is written without dither
    Then it rounds to silence
    When it is written with dither
    Then it comes through at its level, under noise of mean 0 and half a step RMS

  # export_formats_dither_distortion
  Scenario: Dither turns rounding distortion into noise
    Given a tone 1.3 steps high
    Then the harmonics of the rounding error are at least 30 times weaker dithered than not

  # export_formats_dither_deterministic
  Scenario: The same mix writes the same samples
    Then two dithered writes of one signal are identical
    And a float file with dither asked for is the signal exactly

  # export_formats_dither_bounce
  Scenario: A dithered 16-bit bounce
    Given a song bounced through the engine
    Then a dithered 16-bit bounce is the float bounce plus half a step of noise, and repeats exactly

  # bdd_export_formats
  Scenario: DITHER in the window
    Then DITHER is on, and out of play for MP3 and for F32
    And with WAV at 16 bits it toggles, and a dithered export says so in the status line
    And the dithered file differs from the float mix by half a step RMS, the plain one by less
