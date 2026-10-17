Feature: Export as MP3, Ogg, stems and a scope video
  A bounce can be written as MP3 or Ogg Vorbis as well as WAV: the path's
  extension (or the rail's WAV / MP3 / OGG chips) chooses. Every track, and
  every return, can be exported as a file of its own. And the song can be
  rendered as an oscilloscope video in the manner of corrscope: one lane per
  track and a master lane beneath, each held steady by a trigger, with the
  master bounce as its sound, encoded by ffmpeg. All of it is rendered by the
  engine the speakers hear, never a second rendering path.

  # export_formats_mp3, export_formats_ogg, bdd_export_formats
  Scenario: An encoded bounce holds the mix the WAV bounce holds
    Given a song of CLAP instruments
    When it is exported as MP3, and as Ogg Vorbis
    Then each file reads back at the song's rate in stereo, the song's length
    And after aligning for encoder delay it matches the WAV render within 20 dB SNR
    And an export after it is sample for sample the export before it

  # export_formats_extension
  Scenario: The file's extension chooses its container
    Given a path ending .mp3, .MP3, .ogg, .oga, .wav or nothing
    Then it is written as MP3, MP3, Ogg, Ogg, WAV and WAV
    And a stem's file name cannot leave the folder it is exported to

  # export_formats_stems, export_formats_stems_sum, bdd_export_formats
  Scenario: Every track is exported as a file of its own
    Given two panned CLAP tracks and a return fed by a send
    When the stems are exported
    Then the folder holds "01 Lead Hook", "02 Bass" and "03 Room", in mixer order
    And each is that bus's own render, as long as the master bounce
    And with nothing on the master the stems add up to the bounce
    And a muted track's stem is silent, as it was heard
    And the click is never in a stem

  # export_formats_stems_one_pass
  Scenario: Every stem comes from one render of the song
    Given two panned CLAP tracks and a return fed by a send
    When the stems are exported
    Then the song is rendered once, a block at a time, however many stems it writes

  # export_formats_range, bdd_mcp
  Scenario: Some bars of the song are exported
    Given a two-bar song
    When bar 2 alone is bounced
    Then the file is that bar and its tail, and holds what bar 2 of the whole bounce holds
    And its stems are the same stretch
    And a stretch that is empty or runs past the song is refused

  # export_formats_cancel, bdd_mcp
  Scenario: An abandoned render leaves nothing behind
    When a bounce or a stem export is told to stop after a few blocks
    Then it stops there, fails as cancelled and removes what it wrote
    And the next bounce is the song as before

  # bdd_export_formats
  Scenario: The rail chooses the format
    Given the real window
    When the producer clicks MP3 on the rail
    Then MP3 is lit and the WAV depth chips are out of play
    And STEMS and SCOPE VIDEO are usable buttons

  # bdd_export_formats
  Scenario: A scope video of the tracks over the master
    Given three CLAP tracks at different pitches
    When the scope video is exported
    Then it is H.264 at 1280 x 720 and 30 fps, one frame per thirtieth of the song
    And its sound read back matches the master bounce
    And a frame from the middle shows each track's waveform in its own colour, over the master's
