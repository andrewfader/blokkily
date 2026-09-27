Feature: Audio streams from disk through lock-free ring buffers
  Long audio files stream from disk on a background worker thread rather than
  consuming system RAM. The audio callback reads only from pre-buffered
  lock-free ring buffers, never locks, never performs disk I/O, and never
  allocates. If starvation occurs, it softly fades out rather than clicking.

  Executable: tests/disk_streaming_tests.cpp (CTest disk_streaming_<case>)
  through the production render callback and real files;
  tests/realtime/disk_streaming.cpp (realtime_disk_streaming).

  # disk_streaming_playback
  Scenario: Audio streams from disk sample for sample
    Given a stereo 48 kHz WAV file on disk
    And a track configured with a disk stream
    When the song plays through the production callback
    Then the rendered audio matches the file sample for sample
    And no allocations or locks occur on the audio thread

  # disk_streaming_seek
  Scenario: Seeking repositions the stream without blocking or clicking
    Given a playing disk stream
    When the playhead seeks to a new sample position
    Then playback resumes from the new sample offset
    And no clicks or discontinuities occur

  # disk_streaming_underrun
  Scenario: Starvation fades out softly to silence without popping
    Given an exhausted disk stream
    When the audio callback requests audio
    Then the output fades smoothly to silence
    And underrun is reported
