Feature: The window shows what it is loading, and opens quickly
  Opening a song, loading the instruments it plays, picking one from the
  browser, starting a new song and exporting all run on the thread that draws
  the window, because plugins are created, activated and given their state
  there. So the work says how far it has got, and the window covers itself
  with a card naming what is loading, a bar filled as far as the work has got
  and a line saying which instrument is loading ("FUZZBOI · 5 OF 16") or how
  much of the song is written ("1:23 OF 3:10"). Work that ends within a
  quarter of a second is never shown.

  While the card is up nothing typed, clicked or scrolled reaches the song,
  the window cannot be closed under the work, and an agent's MCP requests
  wait until the work is done and are then answered in order. Anything a
  timer or a finished background job would have done to the engine in the
  meantime is done once the work is over.

  Work that goes on behind the window, without stopping anything, is shown by
  a strip along the bottom edge of the transport bar: the plugin scan fills it
  as far as it has got; audio being imported or warped runs a block along it.

  The window itself opens in under half a second: the tour behind HELP does
  nothing until it is asked for, a scan the cache already knows reaches the
  browser in one piece, and a SoundFont track neither opens an audio device
  of its own nor reads its bank once for each of its renderers in turn.

  These scenarios are proved by the bdd_loading_progress gate (`blokkily
  --verify --scenario loading`), which reads every frame a load draws, hears
  the opened song through the production render callback from the CLAP
  fixture and a SoundFont, compares an export read back from disk with the
  same export made with its progress hidden, and saves the window drawn half
  way through the export to build/artifacts/loading-progress.png.

  # bdd_loading_progress "loading: the closed tour looks for nothing while the window is laid out"
  # (a regression: the tour's NEXT/DONE label was a binding over a walk of
  # every item in the window, re-run whenever any of them changed, which was
  # eleven seconds of a release build's start-up)
  Scenario: The closed tour costs the window nothing
    Given the window at its canonical size with the tour closed
    When the window is laid out, made smaller and made larger again
    Then the tour has not looked for any of its stops

  # bdd_loading_progress "loading: opening a song shows its progress and builds its engine once"
  # (a regression: replacing the song built its engine and the load built it
  # again, activating every plugin twice)
  Scenario: Opening a song shows it loading, instrument by instrument
    Given a saved song with the CLAP fixture on one track and a SoundFont on another
    When the producer opens it
    Then every frame drawn while it opens shows the card, at a usable size,
      covering the whole window and titled "OPENING <SONG>"
    And the card names each instrument as it loads, with how many there are
    And the bar never goes back and is drawn as full as the work says
    And once the song is open the card is gone, its engine was built once,
      and it plays what was saved

  # bdd_loading_progress "loading: an export shows how far it has got"
  Scenario: An export shows how much of the song is written
    Given the opened song
    When the producer exports it
    Then every frame drawn while it exports shows the card titled "EXPORTING <FILE>"
    And the card says how much of the song is written, in minutes and seconds
    And the bar fills steadily until the whole song is written, then the card goes

  # bdd_loading_progress "loading: nothing can be clicked or typed while a load is shown"
  Scenario: Nothing reaches a song while it is loading
    Given an export half way through, its card up
    When the producer clicks PLAY and types a note
    Then once the export is done the song is not playing and has no new note

  # bdd_loading_progress "loading: an agent's request waits until the load is over"
  Scenario: An agent's request waits for the load
    Given an agent connected to the window over MCP
    When it asks for the song half way through an export
    Then no answer comes while the export runs
    And the answer comes once it is over

  # bdd_loading_progress "loading: an export writes the same file whether its progress is shown or not"
  Scenario: Showing the progress changes nothing that is rendered
    Given the song exported with its progress shown
    When it is exported again with its progress never shown
    Then the second export draws no frame
    And the two files, read back from disk, hold the same samples, and they are not silent

  # bdd_loading_progress "loading: CANCEL stops an export and leaves nothing behind"
  Scenario: CANCEL stops an export
    Given an export showing its progress
    When CANCEL on it is pressed a few frames in
    Then the export stops, no file is left where it was going, and a notice says it was cancelled
    And the progress closes

  # bdd_loading_progress "loading: the plugin scan fills a strip along the transport bar"
  Scenario: The plugin scan is shown along the transport bar
    Given a folder with a plugin that scans and one that never returns
    When the producer rescans it
    Then while the scan waits on the second plugin a strip runs the width of
      the transport bar, filled as far as the scan has got
    And once the scan is over the strip is gone

  # bdd_loading_progress "loading: a scan the cache already knows reaches the browser in one piece"
  # (a regression: each plugin the cache knew was listed on its own, and the
  # browser was ranked and rebuilt once for every one of them)
  Scenario: A cached scan lists the browser once
    Given four plugins the last scan described
    When the producer rescans without forgetting what it learned
    Then the browser is listed three times: emptied, the four together, and the scan's end
    And all four plugins are in it
