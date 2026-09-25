Feature: Record everything, part 3 - audio input into audio clips
  A producer sets a track's input chip to a pair or a single input of the
  audio device, arms it with its R, and plays. The input is heard through the
  track while it is armed (monitoring AUTO), always (MON) or never (MON OFF),
  on a stopped song too. With the song recording, the raw input is written to
  a WAV file off the audio thread as it is played, and when the song stops it
  becomes an audio clip on the track, placed where the performer heard the
  song: the device's round trip, the engine's output latency (plugin delay
  compensation) and the song's record offset earlier than the input received
  it (plan C21). The clips and the notes of one pass are one step of history.
  Takes are written in <project>.audio/ beside a saved project, and in a
  temporary folder of the session's own until it is first saved, when they
  move into the project's (decision 8). A bounce never hears the inputs.
  While a track takes audio input the device opens duplex, and falls back to
  output only when the server refuses; a song that records no audio keeps an
  output-only stream (plan item 3.2; decisions 3, 4, 5, 8 and 10).

  The engine scenarios are proved by blokkily_audio_input_tests
  (tests/audio_input_tests.cpp, one CTest test per case: audio_input_<case>)
  from what the production render callback rendered through the
  deterministic pump - with input injected into the device, or carried back
  from its outputs by a loopback cable with a known round trip - from the
  production bounce read back from its file, and from the take files read
  back. The interface scenarios are exercised by the bdd_record_audio gate
  (`blokkily --verify --scenario record_audio`,
  src/app/verify/scenario_record_audio.cpp), which names the scenario it
  failed in ("BDD FAIL at: record audio: ..."). realtime_audio_input
  (tests/realtime/audio_input.cpp) proves the callback stays allocation-free,
  and audio_duplex_device_check (tests/audio_duplex_device_check.cpp) opens
  the real audio server duplex, skipping where there is no input device.

  # audio_input_routes
  Scenario: Which tracks hear and record the inputs
    Given tracks whose inputs are MIDI, a stereo pair, and single inputs
    When no track is armed
    Then only the selected audio track records, and only a monitor that is on is heard
    When audio tracks are armed
    Then the armed tracks record, and automatic monitoring follows the arm

  # audio_input_loopback_click
  Scenario: A take sounds exactly where it was heard
    Given a click on the left of the song, and the outputs cabled back to the inputs with a 256-frame round trip
    And a track panned right, armed to record input 1
    When the song plays through once, recording
    Then the take's file holds the click where the input received it, 256 frames late
    And the clip it becomes starts 256 frames earlier, with the frames heard before the song began cut off
    When the song is bounced and read back
    Then the recorded click on the right is within one sample of the original on the left
    And the song's record offset moves a take by exactly that many samples

  # audio_input_latent_master_insert
  Scenario: A take is placed through plugin delay compensation
    Given the same song with the CLAP effect (64 samples of latency) on the master
    When the click is recorded through the cable
    Then the take is placed the round trip plus the 64 samples earlier
    And bounced and read back, the recorded click is within one sample of the original

  # audio_input_overflow
  Scenario: A writer that falls behind costs frames, never the callback
    Given a capture ring with room for 4096 frames and nobody emptying it
    When ten blocks are recorded
    Then 6144 frames are dropped and counted
    And the callback kept rendering and monitoring every block on time
    When the writer catches up and recording goes on
    Then the recording is two takes, each starting at the song sample its first frame was played at
    And every frame of each file is the frame played there

  # audio_input_monitoring
  Scenario: Monitoring is heard but never bounced
    Given an audio track on inputs 1-2 with two steady levels at the inputs
    Then armed with automatic monitoring it is heard on a stopped song
    And with monitoring off, or disarmed on automatic, it is not
    And with monitoring on it is heard without an arm, a mono input on both sides
    And muting the track silences what it monitors
    When the song plays and records
    Then the input is heard over the song
    When the song is bounced
    Then the file equals the arrangement rendered with no input, sample for sample
    And nothing of the bounce was recorded

  # audio_input_take_files
  Scenario: Takes are written as WAV files of the inputs they record
    Given a four-input device, one track on inputs 3-4 and one on input 2
    When they record from sample 1000
    Then each take is its own file, made in a folder created only when needed
    And the pair's file holds inputs 3 and 4, and the mono file input 2, exactly as played
    And the take's audio for the asset store is its file's

  # audio_input_output_only
  Scenario: A device without inputs still plays
    Given an output-only device and an armed audio track
    When the song plays and records
    Then the song is heard and nothing is recorded, no file made

  # audio_duplex_device_check (label device; skips with 77 without an input)
  Scenario: The real audio server delivers input to the callback
    Given a machine with an audio input
    When the device is opened as the application opens it
    Then the stream is duplex and the callback receives input blocks as long as its output blocks

  # realtime_audio_input
  Scenario: Recording and monitoring never make the callback allocate
    Given three tracks recording and monitoring device inputs
    When a thousand blocks are rendered across a seek and loop wraps, the capture ring drained and then full
    Then not one render callback allocates or frees memory

  # gate steps "record audio: a CLAP track, an audio track, and two device inputs",
  #            "record audio: IN 1-2 chosen, monitored and armed from the strip"
  Scenario: A track is set to record an input pair from its mixer strip
    Given a CLAP track and an audio track, and a device with two inputs
    Then the audio track's strip shows an input and a monitor chip big enough to click
    When the producer clicks the input chip, right clicks it, and clicks it again
    Then it steps from MIDI to IN 1-2, back, and to IN 1-2 again
    And the monitor chip steps from MON AUTO to MON, MON OFF and back
    When both tracks are armed
    Then the input and monitor chips are lit and the keyboard plays only the CLAP track
    And no step of history was added

  # gate step "record audio: the armed input is heard while the song is stopped"
  Scenario: An armed input is heard on a stopped song
    Given the audio track armed on inputs 1-2, panned right
    When levels are played into the inputs with the song stopped
    Then the right of the bus carries input 2

  # gate steps "record audio: a key and a burst played while recording",
  #            "record audio: the take is a clip that sounds where it was heard",
  #            "record audio: one undo removes the notes and the clip"
  Scenario: A take becomes a clip in the same step of history as the notes
    Given a device reporting a 128-frame round trip, and recording
    When a key is performed on the CLAP track and a burst is played into the inputs
    Then the burst is heard as it is played
    And when the song stops it is a clip on the audio track, in the session's temporary folder
    And played back it sounds 128 frames before it reached the input, to the sample
    And the clip is drawn in the audio track's lane
    When the producer undoes once
    Then the notes and the clip are both gone, and redo brings both back

  # gate steps "record audio: the take moves into the project's audio folder on save",
  #            "record audio: the export holds the take on the same sample"
  Scenario: Takes move into the project's audio folder on the first save
    Given a take recorded in an unsaved session
    When the session is saved as record-audio.blok
    Then the take's file is in record-audio.audio/ and no longer in the temporary folder
    And undo and redo keep naming it there
    When the project is loaded again
    Then the clip plays its file from the project's folder on the same sample
    And the export read back from its file holds the take on the same sample
