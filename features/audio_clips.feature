Feature: Audio clips play in the song, sample for sample
  An audio file placed on a track's timeline is an audio clip. It plays
  through its track's strip beside whatever the track's instrument plays,
  starts on the sample its tick falls on under the tempo map, and plays the
  stretch of the file it was trimmed to, with its own gain and fades.
  Overlapping clips on one track all sound (decision 6). Imported files are
  referenced where they are (decision 8), stored relative to the project
  folder when they lie inside it, and a file that has gone or changed is
  flagged rather than played wrong. (Item 2.2.)

  Executable: tests/audio_clips_tests.cpp (CTest audio_clips_<case>) through
  the production render callback and the production bounce on real files;
  tests/realtime/audio_clips.cpp (realtime_audio_clips); and the gate
  bdd_audio_clips (src/app/verify/scenario_audio.cpp), whose reached() labels
  are quoted below.

  # audio_clips_start_sample
  Scenario: A clip starts on the exact sample of its tick
    Given a song at 120 BPM with 480 ticks a beat at 48 kHz
    And a clip of a 48 kHz file placed at tick 700 on a track with no instrument
    When the song plays through the production callback
    Then nothing sounds before sample 35000
    And from sample 35000 every frame of the file sounds through the track's strip, in order

  # audio_clips_offset_length
  Scenario: A trimmed clip plays only its stretch of the file
    Given a clip that starts 1000 frames into its file and lasts 5000 frames
    When the song plays
    Then the clip plays frames 1000 to 5999 of the file and then stops

  # audio_clips_gain
  Scenario: A clip's gain scales what it plays
    Given a clip at -6.02 dB
    When the song plays
    Then every sample of the clip is the file's sample times 10^(-6.02/20)

  # audio_clips_fades
  Scenario: Fades ramp a clip in from silence and out to silence
    Given a clip of a constant file with a 1000-frame fade-in and a 2000-frame fade-out
    When the song plays
    Then its first frame is silent and the level rises on every frame of the fade-in
    And it is at full level between the fades
    And the level falls on every frame of the fade-out to silence on its last frame

  # audio_clips_mixer_live
  Scenario: Mute, solo, pan and the fader act on a clip's track while it plays
    Given two tracks playing the same clip, one of them muted
    When the other is muted, then one is soloed, then panned hard left at +6 dB
    Then the bus goes silent, then carries exactly one clip, then only the left channel at +6 dB
    And the engine is never prepared again

  # audio_clips_recompile_continuity
  Scenario: Editing clips while the song plays does not interrupt it
    Given a clip playing
    When the arrangement is recompiled every other block
    Then what is heard is identical, sample for sample, to a run without recompiles
    And a clip moved later sounds from its new place without the playhead moving

  # audio_clips_clap_sum
  Scenario: A clip on an instrument's track sums with the instrument
    Given the CLAP fixture playing a note on a track that also holds a clip
    When the song plays
    Then every sample is the instrument alone plus the clip alone, within 1e-6

  # audio_clips_overlap_sum
  Scenario: Overlapping clips on one track both sound
    Given two clips on one track overlapping by 5000 frames
    When the song plays
    Then where they overlap the bus carries the sum of both

  # audio_clips_bounce_readback
  Scenario: A bounce is the clips the engine plays, sample for sample
    Given a song with a 48 kHz clip and a 44.1 kHz clip with gain, fades and pan
    When it is bounced to a float WAV and the file is read back
    Then every frame equals what the production callback played

  # audio_clips_tempo_map
  Scenario: A clip after a tempo change starts where the tempo map puts it
    Given a song that steps from 120 BPM to 60 BPM at bar 2
    And a clip placed at tick 2400
    When the song plays
    Then the clip starts at clock.sample_at(2400), sample 144000
    And the song lasts long enough to play all of it

  # audio_clips_resampled_file
  Scenario: A file at another sample rate plays at its own pitch and length
    Given a one-second 1 kHz file at 44.1 kHz placed on a beat
    When it plays at 48 kHz
    Then it sounds at 1 kHz within 1 Hz, from its beat, for 48000 samples
    And a mono file sounds on both channels

  # audio_clips_project_paths
  Scenario: A project folder moved whole still finds its audio
    Given a project whose audio file lies in its own folder
    When it is saved, the folder is moved, and the project is opened from the new place
    Then the file was stored relative to the project
    And it resolves inside the moved folder and plays

  # audio_clips_missing_and_changed
  Scenario: A missing or changed file is flagged and stays silent
    Given a song whose files are one removed, one replaced with other audio, and one intact
    When the song is prepared
    Then the removed and the replaced file are reported missing, with the reason
    And neither is played while the intact one is
    And a file put back as it was is found and played on the next recompile

  # audio_clips_adopted_import
  Scenario: An import decoded off the control thread is not decoded again
    Given a file decoded at the engine rate by an import worker's own asset store
    When the application's store adopts it and the song's clip assets are loaded
    Then the clip plays that same decode and the file is not read again
    And a file changed on disk afterwards is reported missing

  # realtime_audio_clips
  Scenario: Audio clips play without the render callback allocating
    Given overlapping clips with gain and fades on two tracks, one resampled
    When a thousand blocks play across a seek and loop wraps while another thread recompiles
    Then no process() call allocates or frees memory
    And the clips were heard

  # bdd_audio_clips: "audio clips: +AUD asks for a file, and a cancel changes nothing"
  Scenario: +AUD asks for an audio file
    Given the arrangement
    When +AUD is clicked
    Then the import dialog opens
    And cancelling it leaves the song as it was

  # bdd_audio_clips: "audio clips: an import decodes in the background and lands as one step"
  Scenario: Importing a file does not hold up the interface
    Given a new audio track with no instrument
    When a four-second file is imported onto it at bar 2
    Then the song is unchanged until the decode has finished off the control thread
    And then one clip of the whole file is on the track from bar 2
    And one undo takes back the clip and the file, and redo returns them

  # bdd_audio_clips: "audio clips: the lane places the clip on its bars, with its waveform"
  Scenario: The clip is drawn on the bars it plays over
    Then the clip is drawn on its track's row from the ruler's bar 2 to the start of bar 4
    And it is large enough to use and shows its waveform

  # bdd_audio_clips: "audio clips: the clip is heard sample for sample, and its strip mutes it"
  Scenario: The clip is heard through the production callback
    When the song plays across the clip's start
    Then the callback renders silence and then the file's samples through the track's strip
    And muting the track silences it

  # bdd_audio_clips: "audio clips: dragging moves the clip on the grid and between tracks"
  Scenario: Dragging a clip moves it
    When the clip is dragged two bars later with the mouse
    Then it starts at bar 4, keeps its id, and is heard there and no longer at bar 2
    When it is dragged up one row
    Then it is on the track above, and undo puts it back

  # bdd_audio_clips: "audio clips: trimming either edge reveals or hides the file in place"
  Scenario: Trimming a clip's edges
    When its end edge is dragged back half a bar and its start edge forward a beat
    Then the clip lasts 120000 frames from frame 24000 of the file
    And each remaining frame still sounds at the sample it sounded at before

  # bdd_audio_clips: "audio clips: fades and gain shape what is heard, and undo takes them back"
  Scenario: Fades and gain
    When the fade handles are dragged a beat inwards and the wheel turns the clip down six notches
    Then the clip fades in from silence, and between the fades it is heard at -6 dB
    And one undo brings it back to 0 dB and redo to -6 dB, each heard

  # bdd_audio_clips: "audio clips: saved relative, bounced as heard, a missing file flagged"
  Scenario: Saving, bouncing and reopening a song with clips
    Given a second clip of a 44.1 kHz file
    When the project is saved
    Then both files are stored relative to the project folder
    And the bounce, read back, equals what the callback plays, sample for sample
    When the project is reopened
    Then both clips come back with their ids and play
    When one file is removed and the project is reopened
    Then its clip is flagged missing on the lane and is silent, and the other still plays
