Feature: Record and bounce mixer outputs through the playback engine
  Scenario: Master resampling (output_recording_live, bdd_output_recording)
    Given a song with real CLAP instruments, an insert and a metronome
    When I record the master output through the production callback
    Then the written take equals the read-back bounce sample for sample after latency compensation
    And the guide click is excluded
    And the take becomes an audio clip on a new track that can replace the muted sources

  Scenario: Aligned stems (output_recording_stems, output_recording_returns)
    Given tracks with inserts, pan and faders and a return fed by a send
    When I bounce the track and return outputs
    Then each file contains its post-insert post-fader stereo signal
    And the aligned track and return files sum to the unity master mix

  Scenario: Invalid sources and feedback (output_recording_invalid, bdd_output_recording)
    Given an output recorder
    When I choose a nonexistent bus
    Then the engine rejects the request
    And live recording creates its destination only after the take ends

  Scenario: Bounded recording cost (realtime_output_recording)
    Given a full capture ring
    When the engine renders track, return and master taps
    Then it drops capture frames without waiting or allocating

  Scenario: Output actions use the selected rack (bdd_output_recording)
    Given the master rack in the real application
    When I arm RESAMPLE and record a pass
    Then a new audio track holds the output and one undo removes the take
    When I bounce and mute the source
    Then a stem appears on a new track and undo restores the original mixer

  Scenario: Export leaves the producer's parameter state intact (output_recording_state)
    Given a real CLAP instrument with a parameter lock later in the song
    When I export twice
    Then the two read-back files contain identical samples
    And the first export has not left its final lock in the next playback

  Scenario: SoundFont exports start from silence (output_recording_soundfont_reset)
    Given a real SoundFont rendered through FluidSynth
    When I export the same song twice
    Then the two read-back files contain identical samples
