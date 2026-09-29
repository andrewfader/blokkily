Feature: Long audio clips stream from disk
  An audio clip whose file would take more memory decoded than the streaming
  threshold (AudioAssetCache::set_stream_threshold, 128 MiB by default, the
  BLOKKILY_STREAM_THRESHOLD_BYTES environment variable in the application)
  is not decoded into RAM. Each such clip plays through a ring buffer that a
  background worker fills from the file, resampled to the engine rate with
  the converter the in-memory path uses. The render callback only reads the
  ring and asks for positions through one atomic word: it never locks, reads
  the file or allocates. It follows the song: a clip about to start is cued
  to its first frame, a locate or a loop wrap into a clip asks for the new
  position, and a clip at the song's start is cued again before the song
  wraps. A ring that runs dry fades out over audio it keeps in reserve and
  fades back in when the disk catches up, staying in time with the song.
  An export reads the file on its own thread before each block, so it never
  starves and is the engine's render sample for sample.

  Warped clips play a rendition rendered from the file decoded into memory,
  because a stretch reads the whole clip; streaming does not apply to them.
  The sampler never streams. One stream serves one clip, so a single clip
  that plays across the song's wrap point relocates at the wrap like a locate
  (a short fade out and in) rather than playing on seamlessly.

  Executable: tests/disk_streaming_tests.cpp (CTest disk_streaming_<case>),
  through the production render callback, the production bounce and real
  files; tests/realtime/disk_streaming.cpp (realtime_disk_streaming).

  # disk_streaming_playback
  Scenario: A long clip streams from disk and sounds as it would from memory
    Given a WAV file larger than the streaming threshold
    And a clip of it with an offset, a gain and fades
    When the song plays through the production callback
    Then the clip's asset holds no samples in memory
    And the rendered audio equals the same clip played from memory, sample for sample

  # disk_streaming_resampled
  Scenario: A streamed file at another rate is resampled as it is in memory
    Given a 44.1 kHz file streamed into a 48 kHz song
    When the stream is read from its start and after seeks into the file
    Then every frame matches the in-memory resampled file to within 2e-5
    And the clip keeps its pitch

  # disk_streaming_seek
  Scenario: Locating inside a streamed clip resumes there without a click
    Given a streamed clip playing
    When the playhead is moved into the middle of the clip before the disk answers
    Then what was playing fades out and the clip is silent while it waits
    And no step between neighbouring samples exceeds the tone's own plus a short fade
    And once the disk answers the clip plays the file at the new song position exactly

  # disk_streaming_underrun
  Scenario: A stream the disk cannot keep up with dips and recovers without clicks
    Given a streamed clip playing
    When the disk stops answering until the ring runs dry and then answers again
    Then the output fades out over buffered audio instead of cutting
    And fades back in on recovery with no click
    And the audio before the dip and after the recovery is the file at the song's position

  # disk_streaming_loop_cue
  Scenario: A clip at the start of the song is ready again when the song wraps
    Given a streamed clip on the song's first beat
    When the song plays through twice
    Then the second pass equals the first, sample for sample

  # disk_streaming_bounce
  Scenario: An export of streamed clips is the mix the engine plays
    Given a song with a streamed 48 kHz clip and a streamed 44.1 kHz clip
    And the engine's disk worker running on its own thread
    When the song is bounced after playing elsewhere
    Then the file reads back equal to the live streamed render, sample for sample
    And equal to the in-memory render within resampling rounding

  # realtime_disk_streaming
  Scenario: Streaming never allocates on the audio thread
    Given a streamed clip, a dry ring and a locate
    When SongEngine::process runs 1000 times
    Then no allocation happens inside it
