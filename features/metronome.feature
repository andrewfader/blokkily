Feature: A metronome and a count-in to play along to
  A click follows the song's tempo and meter maps: an accented click on every
  downbeat and a plainer one on every other beat of the meter's beat unit (a
  quarter in 4/4, an eighth in 7/8), each starting on the sample the tick
  clock puts its beat on. It is heard live through a path of its own into the
  output, after the master strip, so no track's solo or mute, no insert and
  no fader reaches it, and a bounce leaves it out unless the export asks for
  it. A count-in plays 0 to 4 bars of click before a recording starts; the
  song position does not move while it plays, nothing played into it is
  recorded, and a note still held when the song starts is recorded from
  there. The settings are saved with the project and undo leaves them alone.
  (Plan item 3.7, decision 14.)

  # metronome_clicks_on_tempo_step
  Scenario: The click follows a tempo step
    Given four bars of 4/4, two at 120 BPM and two at 90 BPM, with the click on at 0 dB
    When the song plays through the production callback
    Then clicks start at samples 0, 24000 ... 168000 and 192000, 224000 ... 416000, each within one sample
    And those are the samples the tick clock puts beats 0 to 15 on
    And the downbeat clicks peak more than half as loud again as the other beats

  # metronome_clicks_in_seven_eight
  Scenario: The click follows a 7/8 meter
    Given a bar of 4/4 and two bars of 7/8 at 120 BPM
    When the song plays with the click on
    Then the 4/4 bar clicks every 24000 samples and the 7/8 bars every eighth, 12000 samples
    And the downbeats of bars 1, 2 and 3 are the accented clicks

  # metronome_click_level_and_switch
  Scenario: The click has its own level and switch
    Given the click on at 0 dB
    When its level is set to -12 dB on the running engine
    Then the next pass's click is 0.251 as loud
    And with the click off a song with nothing else is silent
    And no click sounds while the transport is stopped

  # metronome_click_ignores_solo_and_mute
  Scenario: Solo, mute and the master fader do not reach the click
    Given two CLAP tracks, hard left and hard right, playing between the beats
    When every track is muted, then one track is soloed and the master fader pulled down 24 dB
    Then the tracks leave the bus as the mixer says
    And every sample of every click is what it was with the mixer open

  # metronome_bounce_click_option
  Scenario: A bounce leaves the click out unless asked
    Given a CLAP song with the click on
    When it is bounced with the default options and read back
    Then the file is the engine's render without the click, sample for sample
    When it is bounced with the click asked for and read back
    Then the file is the engine's render with the click, sample for sample, opening on the downbeat's click
    And the session's click is still on afterwards

  # metronome_count_in_bars
  Scenario: A count-in plays N bars before the song, which waits where it is
    Given a song with its playhead on bar 2 and a two-bar count-in, with the click off
    When the transport starts with the count-in
    Then exactly eight clicks sound, every 24000 samples, the first of each bar accented
    And the playhead stays on sample 96000 and the engine reports the count-in throughout
    And the song's first sample, its note on bar 2's downbeat, sounds at output sample 192000
    And a one-bar count-in in a 7/8 bar is seven eighth clicks, 84000 samples

  # metronome_count_in_recording
  Scenario: Notes played into the count-in
    Given recording with a one-bar count-in, and a MIDI port playing the CLAP track
    When a key is pressed and released inside the count-in, and another is pressed inside it and held into the song
    Then both are heard as they are played
    And only the held key is captured: its note-on at song sample 0, tick 0, at its velocity
    And its release where it was heard, so the take is one note from the song's first tick

  # metronome_project_records
  Scenario: The settings are saved with the project
    Given the click on at -12.5 dB with a three-bar count-in
    When the project is saved and loaded
    Then the settings come back as they were, and the file saves byte for byte the same
    And a project that never touched them saves no metronome record and loads with the click off
    And a malformed or second metronome record is refused

  # realtime_metronome
  Scenario: The click and the count-in keep the real-time rules
    Given the click on in a song of 7/8 and 4/4 with a tempo ramp
    When a count-in plays while recording, with notes from a MIDI port, then the song loops and is sought, and a second count-in is stopped
    Then SongEngine::process allocates and frees nothing
    And the note held into the song and its release are captured

  # bdd_metronome "metronome: the transport offers CLICK, a count-in and a level, each usable"
  Scenario: The transport has the metronome's controls
    Given the app open on a 1280 by 800 window
    Then the transport shows CLICK, the count-in chip reading "NO CI" and the level slider reading "-6.0"
    And the export heading shows "+ CLICK"
    And each is big enough to use, and the transport still fits the window to RESCAN PLUGINS

  # bdd_metronome "metronome: CLICK and the level slider set the song's click"
  Scenario: CLICK and the level slider set the click
    When CLICK is clicked
    Then it lights and the running engine plays the click
    When the level slider is pressed at its top
    Then the level reads "+6.0"

  # bdd_metronome "metronome: the click is heard on every beat, the downbeat louder, whatever the mute"
  Scenario: The click heard with the song
    Given a CLAP song at 120 BPM with the click on at -6 dB
    When Play is clicked
    Then the production callback opens a 0.501 click on each downbeat and a 0.251 click on each other beat
    When the track's M is clicked
    Then the song's note is gone and the downbeat's click is still there

  # bdd_metronome "metronome: a two-bar count-in clicks 8 beats while the playhead waits on bar 2"
  # bdd_metronome "metronome: a key held into the song start is recorded on its first step"
  Scenario: A count-in into a take
    Given the count-in chip clicked to "CI 3" and right-clicked back to "CI 2", and R armed
    And the playhead on bar 2
    When Play is clicked
    Then eight clicks sound while the playhead stays on bar 2, and the count-in chip is lit
    And the song's downbeat click follows at output sample 192000
    When a key is played in the count-in's second bar and held into the song, then released and the song stopped
    Then the pattern under bar 2 holds that key on step 1 with no micro-timing, drawn in the grid

  # bdd_metronome "metronome: undo takes back the take and leaves the click as set"
  Scenario: Undo leaves the metronome alone
    When the click level is changed and the take is undone
    Then the take is gone and the click, its level and the count-in are as they were set

  # bdd_metronome "metronome: the bounce leaves the click out unless + CLICK is lit"
  Scenario: The export's click option
    When the song is bounced with "+ CLICK" beside the EXPORT heading unlit
    Then the file holds no click on any beat
    When "+ CLICK" is clicked, lights, and the song is bounced again
    Then the second file less the first is the click, on every beat

  # bdd_metronome "metronome: the click and count-in are saved and loaded with the project"
  Scenario: The metronome is saved with the project
    When the project is saved
    Then the file holds "metronome on -6 2"
    And after the settings are changed and the project loaded again, the click is on at -6 dB with a two-bar count-in, in the engine and on screen
