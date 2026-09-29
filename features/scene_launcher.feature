Feature: Scene launcher
  A grid of scenes (rows) by tracks (columns) whose cells name the song's
  patterns - the same patterns the step grid, tracker and piano roll edit.
  The grid is part of the song (Song::launcher): saved with the project,
  undone with it, and compiled into the engine with the arrangement, so an
  edit reaches a running song as a recompile, never a rebuild. Launches and
  stops reach the render callback through a lock-free queue and happen on
  the boundary their quantization allows: a cell launched alone waits for its
  own quantization, a whole scene and every stop for the grid's. A launched
  track plays its cell instead of its arrangement; stopped in the launcher it
  stays silent until the transport stops, and is then its arrangement's
  again. With arrangement recording on, every stretch a track plays from one
  cell is printed into the arrangement as clips when it ends, one step of
  undo per take, and the printed arrangement plays and exports exactly what
  was heard.

  A rebuild of the graph (a track added or removed, an instrument changed)
  carries the launcher into the new engine: launched cells play on in phase,
  queued launches still land, held notes are let go where their pattern
  says, an open take carries on, and the playhead stays where it was. The
  takes the old engine finished are printed before it goes. A take printed
  over a clip splits it: what the clip played before and after the take both
  stay (the tail as a clip trimmed with Clip::offset). A full command or take
  queue refuses or drops what does not fit, counts it and says so in the
  status line.

  Not done: a scene has no tempo of its own (the song's tempo map is the only
  tempo); an export is the arrangement, so launched tracks are not in it; a
  launched pattern with probability repeats its random choices every 16
  loops (or its loop conditions' cycle, up to 64), so that its takes print
  exactly. A trimmed clip does not chase a controller value set before its
  trim.

  Executable: tests/scene_launcher_tests.cpp (CTest scene_launcher_<case>),
  tests/realtime/scene_launcher.cpp (realtime_scene_launcher), and the
  bdd_launcher gate (src/app/verify/scenario_launcher.cpp).

  # scene_launcher_quantized_launch
  Scenario: A scene launch waits for the bar, a cell for its own quantization
    Given two tracks, each with a cell in scene A, and the transport playing
    When scene A is launched in the middle of bar 0
    Then nothing sounds until bar 1, and both cells sound from its first sample
    And a stop of every track silences them on the next bar exactly
    And a cell launched alone with beat quantization starts on the next beat

  # scene_launcher_downbeat_after_wrap
  Scenario: The downbeat of every loop is played
    Given a launched one-bar cell with a note on its downbeat, on a song of an odd length
    When it loops through blocks that its turns and the song's wraps fall inside
    Then the downbeat sounds on the first sample of every loop, and the offbeat too

  # scene_launcher_follow_actions
  Scenario: Follow actions move between scenes
    Given a cell that plays twice then follows "next", one that follows "random", and one that follows "first"
    When the first is launched
    Then its two loops are heard, then the next scene's cell
    And "random" goes to another scene with a cell, "first" back to the top
    And two runs from the same seed follow the same path

  # scene_launcher_follow_across_wrap
  Scenario: A follow action on a song exactly one loop long
    Given a song one bar long and a cell that follows "next" after one loop
    When the loop's end is the song's wrap
    Then the next scene's cell is heard from the wrap

  # scene_launcher_stop_releases
  Scenario: Stopping lets go of what the cell holds
    Given a launched cell whose note rings past the end of its loop
    When its follow action stops it, when it is stopped on the bar, and when the transport stops
    Then the audio falls to silence on the boundary sample each time

  # scene_launcher_arrangement_hand_back
  Scenario: A launched track leaves its arrangement until the transport stops
    Given a track playing its arrangement
    When a cell is launched on it, then stopped in the launcher
    Then the cell replaces the arrangement from its first sample, and the stopped track is silent
    And after the transport stops the arrangement is heard again

  # scene_launcher_record_prints_arrangement
  Scenario: Arrangement recording prints what was launched
    Given arrangement recording on, and a pattern whose second note plays every other loop
    When it is launched for three loops, another cell follows on the bar, and that is stopped mid-note
    Then two takes come back, printed as clips of whole loop cycles with the last cut where the stop fell
    And the printed arrangement played by a fresh engine is the launched performance, sample for sample
    And its export, read back, is the same samples again

  # scene_launcher_edit_while_launched
  Scenario: Editing the song while a cell plays
    Given a launched cell
    When its pattern is edited, a pattern before it is deleted, and then its own pattern is deleted
    Then the edit is heard from the next loop, the cell follows its pattern down the list
    And the emptied cell stops its track and lets go of its note at once

  # scene_launcher_serialization
  Scenario: The grid is saved with the song
    Given scenes named "Verse 2", "" and one with a percent sign, quotes and a tab, and cells of every kind
    When the project is saved and loaded
    Then every scene, name, cell and the grid's quantization come back, and a cut clip keeps its cut
    And a file naming a quantization, a follow action or a pattern that does not exist is refused
    And a file from before names were escaped, with scene tempos, still loads

  # scene_launcher_survives_rebuild
  Scenario: A launched loop plays on in phase through a rebuild
    Given a launched cell whose note is held a quarter of the way into its second loop
    When a track is added at the front, so the graph is rebuilt and the jam's track moves
    Then the new engine shows the cell playing on the moved track before it renders
    And the master bus is sample for sample what an engine never rebuilt plays
    And the held note is let go where the pattern says

  # scene_launcher_rebuild_keeps_takes
  Scenario: Takes survive a rebuild
    Given arrangement recording on and a cell followed by another on the bar
    When the graph is rebuilt after the first take ended and while the second is open
    Then the first take comes back from the old engine before it goes
    And the second is finished by the new engine where the stop lands

  # scene_launcher_full_queues
  Scenario: Full launcher queues are counted, not fatal
    Given a cell that relaunches itself every loop with recording on
    When 300 launches are sent without the callback running, and 1100 takes are made without being drained
    Then 256 launches are queued and 44 refused and counted
    And the takes that did not fit are counted, the queue holds what it can, and the loops play on

  # scene_launcher_print_keeps_tail
  Scenario: A take printed over a longer clip keeps the clip's tail
    Given a four-bar clip whose pattern has a step on every other loop
    When a take is printed over bars 1.5 to 2.5
    Then the clip is split into its head and a tail trimmed to where it had got to
    And before and after the take the song renders exactly as it did, the alternating step included
    And the tail saves as a trimmed clip, reads back byte for byte, and bad trims are refused

  # realtime_scene_launcher
  Scenario: The launcher keeps the real-time rules
    Given two CLAP tracks launched from a grid with follow actions and arrangement recording on
    When three thousand blocks play across scene launches, a stop, a seek, a song wrap and a recompile
    Then the render callback never allocates or frees, the cells sounded, and takes came back

  # bdd_launcher
  Scenario: A producer jams in the LAUNCH view and records it
    Given the application with two CLAP tracks and three patterns
    When LAUNCH is picked in the view switcher, two scenes are added and cells filled by clicking the grid
    And the open pattern for each cell is chosen by clicking its chip
    And a cell is given the follow action NEXT and the scenes the quantization BEAT from the pickers
    And REC ARRANGEMENT is switched on and a scene launched from its chip
    Then the scene is heard from the first block and its follow action a loop later
    When a track is added mid-jam from +TRK, rebuilding the graph
    Then the take the follow action finished is printed first
    And both cells play on through the new engine from where the playhead was, silent in their notes' gap before the next bar
    And the stop chips silence their tracks on the beat
    And every take is printed into the arrangement, one undo step each
    And the project saves, loads and exports with the grid and the takes
    And deleting a pattern re-indexes the cells that name later ones
    And 300 launches sent at once queue what fits and the refused ones are counted and shown in the status line
