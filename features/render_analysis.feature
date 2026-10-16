Feature: Measuring a render the way an engineer listens to it
  After printing a mix, a producer reads a loudness meter and a spectrum
  analyser: how loud the song is against a streaming target, whether the true
  peak will clip a converter, and which band is crowded (mud in the low mids,
  boom in the bass, harshness in the presence band). Blokkily measures a
  render the same way, so an agent can print, read what it hears, and fix the
  mix: integrated loudness and loudness range as ITU-R BS.1770-4 and EBU Tech
  3342 define them, sample and true peak, RMS and crest factor, left/right
  correlation, the spectral centroid, and each band's level and share of the
  energy.

  The measurements are proved against signals whose answers are known, the
  loudness ones being the EBU's own test cases. Each scenario maps to a test of
  blokkily_analysis_tests (tests/analysis_tests.cpp), named analysis_<case>.
  What an agent measures through the running window is the bdd_mcp scenario
  "The agent measures what it made" (features/mcp.feature).

  Scenario: A steady tone reads its loudness (analysis_loudness)
    Given a stereo 1 kHz sine at -23 dBFS, and another at -33 dBFS, at 48 and 44.1 kHz
    When each is measured
    Then the integrated, loudest momentary and loudest short-term loudness read -23 and -33 LUFS within 0.1 LU
    And the loudness range is 0 LU, the sample peak is the sine's level, and RMS is 3 dB under it

  Scenario: Quiet passages do not pull the loudness down (analysis_gating)
    Given 60 s of a -23 dBFS sine between passages at -36 and -72 dBFS
    When it is measured
    Then it reads -23 LUFS, because the passages fall below the absolute and relative gates
    And the loudest 3 s is placed inside the loud passage

  Scenario: Loudness range spans the passages (analysis_loudness_range)
    Given 20 s at -20 dBFS followed by 20 s at -30 dBFS, or at -15 dBFS
    When it is measured
    Then the loudness range is 10 LU, or 5 LU, within 1 LU

  Scenario: True peak finds the crest between samples (analysis_true_peak)
    Given a full-scale sine at a quarter of the sample rate whose samples all fall 45 degrees off its crests
    When it is measured
    Then the sample peak reads -3 dBFS and the true peak 0 dBTP
    And a slow sine's true peak is its sample peak, and a sample past full scale is counted as clipped

  Scenario: Energy is reported in the band that holds it (analysis_bands)
    Given a -6 dBFS sine in each of the sub, bass, lowMid, mid, highMid, presence and air bands
    When each is measured
    Then nearly all its energy is in its own band, at the sine's RMS level, and the centroid is its frequency
    And two equal sines at 100 Hz and 3 kHz split the energy evenly with the centroid between them

  Scenario: Stereo image and silence (analysis_stereo_and_silence)
    Given the same sine in both channels, then one channel inverted
    When each is measured
    Then the correlation reads +1, then -1
    And silence, or nothing at all, has no loudness, level, correlation or centroid
