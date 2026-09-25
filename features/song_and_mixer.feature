Feature: Arrange patterns into a song across mixer tracks
  A pattern sequencer becomes a workstation when several instruments can play
  different patterns at once and the result can be balanced and exported.

  Scenario: A session is a set of tracks, not a single instrument
    Given Blokkily is running with a CLAP, a VST3, and a SoundFont fixture
    Then the mixer lists one strip per track, each naming its own instrument
    And each strip offers gain, pan, mute, and solo, plus a peak meter
    And the arrangement shows one lane per track

  Scenario: A chosen instrument loads onto the selected track
    Given the producer selects a track in the mixer
    When they choose an instrument in the browser
    Then that track carries the instrument
    And the other tracks keep the instruments they had

  Scenario: Clips place a pattern in time
    Given the arrangement has a VERSE clip of 2 bars on the first track
    And a CHORUS clip of 2 bars from bar 3
    Then the song lasts 4 bars
    And the first track's timeline reports bars 1 through 4 as filled
    When the producer clicks an empty bar on another lane
    Then a clip of the pattern currently open appears there
    When the producer right-clicks it
    Then the clip is removed

  Scenario: Clicking a clip opens the pattern it plays
    Given a CHORUS clip on the second track from bar 3
    When the producer clicks that clip
    Then CHORUS is the open pattern in every editor
    And the second track is selected
    And the audio engine's playhead is at the start of bar 3
    And the clip is still there

  Scenario: A clip can be lengthened without placing another
    Given a one-bar VERSE clip on the first track
    When the producer lengthens it to two bars
    Then the timeline reports two filled bars of one clip
    And that clip's repeats are two rather than two separate placements

  Scenario: A clip can be moved along its lane
    Given a VERSE clip starting at bar 1 on the first track
    When the producer moves it to start at bar 3
    Then bar 1 is empty
    And the clip starts at bar 3 with the same length

  Scenario: Each repetition of a clip is the next loop of its pattern
    Given a pattern whose step plays every other loop
    And a clip that repeats that pattern twice
    Then the step sounds in the first repetition and not in the second

  Scenario: Clicking the arrangement ruler locates the song
    Given an arrangement of several bars
    When the producer clicks bar 2 of the rendered ruler
    Then the audio engine's playhead is at the start of bar 2
    And every editor draws the playhead there
    And the clip that sat on bar 2 is still the clip that was there

  Scenario: Switching the open pattern moves every editor
    Given the arrangement holds a VERSE and a CHORUS pattern
    When the producer selects CHORUS
    Then the step grid, tracker, and piano roll all show the CHORUS events
    And the editor header names CHORUS

  Scenario: The mixer changes what the bus carries
    Given two tracks are playing at unity gain, panned centre
    Then each contributes its own level to both sides of the bus
    When one track is muted
    Then it contributes nothing and the other is unchanged
    When the other track is soloed instead
    Then only the soloed track reaches the bus
    And a strip that is muted stays silent even when it is also soloed

  Scenario: Panning holds its power across the image
    Given a strip at unity gain
    Then a centred strip sits 3 dB down on each side
    And a hard-panned strip is unity on its side and silent on the other
    And the summed power is unchanged at every position in between

  Scenario: A fader move does not interrupt playback
    Given the song is playing
    When the producer changes a track's gain, pan, mute, or solo
    Then the change reaches the running engine
    And the audio graph is not rebuilt

  Scenario: The arrangement can be bounced to a file
    Given a prepared arrangement
    When the producer bounces it to a wave file
    Then the file is stereo at the session sample rate
    And it is as long as the song plus the requested tail
    And it is sample-for-sample what the same engine renders in real time
    And the transport is left where the bounce found it

  Scenario: A bounce can be written at any supported depth
    Given a mix that reaches full scale
    When it is written as 16-bit, 24-bit, or 32-bit float
    Then each file reads back with the same shape and sample rate
    And a signal past full scale clamps instead of wrapping polarity

  Scenario: A song that was never prepared cannot be bounced
    When a bounce is requested for an engine with no song
    Then no file is written
    And the reason is reported

  Scenario: Stopping releases arrangement notes
    Given a sustained arrangement note is sounding through the CLAP instrument
    When the producer stops playback before its note-off
    Then the next audio callback releases that note without advancing the playhead
    And playing live notes on the stopped transport still produces audio

  Scenario: An export tail never starts another pass of the arrangement
    Given an arrangement with a sustained CLAP note from its first tick
    When the producer exports the song with a release tail
    Then the written file contains the arrangement once
    And its tail contains no restarted notes

  Scenario: Rewind moves the sound and the display together
    Given the song is playing through the production audio callback
    When the producer clicks the rewind button
    Then the next callback plays from the beginning of the arrangement
    And the transport display follows that position

  Scenario: Export borrows the audio device and returns it
    Given playback is running through the production audio callback
    When the producer exports the arrangement
    Then the device is suspended while the export uses the engine
    And playback resumes at its previous position after success or a write failure

  Scenario: The whole session survives being saved and reloaded
    Given a session with two patterns, three tracks, and its clips
    And a track that has been muted and pulled to -7.5 dB
    When the producer saves the project and edits it further
    And the producer reloads the saved project
    Then the patterns, tracks, clips, and mixer settings all come back
    And every instrument returns with the state it was saved with
    And a clip that names a track or pattern which does not exist is refused
