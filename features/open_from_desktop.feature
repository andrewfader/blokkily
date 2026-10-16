Feature: Open a .blok from the desktop
  Double-clicking a project in a file manager (GNOME Files, COSMIC Files or
  any desktop following the freedesktop standards) opens it in Blokkily. The
  MIME package defines application/x-blokkily-project for *.blok files and
  for any file that begins "blokkily-project"; the desktop entry claims that
  type and hands the file to the program, which opens it at launch.

  # desktop_integration
  Scenario: The desktop knows a .blok is a Blokkily project
    Given the installed desktop entry and MIME package
    When a desktop compiles the package with update-mime-database
    Then *.blok, and a project's first bytes, map to application/x-blokkily-project
    And the desktop entry claims that type, passes the file with %f, and validates

  # bdd_open_file_setup, bdd_open_file_path, bdd_open_file_url
  Scenario: Launched with a project, the window opens that song
    Given a saved project named "opened song.blok" with its own tracks
    When blokkily is started with its path, or with its file:// URL
    Then the window holds that song's tracks, named "opened song"
    And nothing is unsaved

  # bdd_open_file_missing
  Scenario: A launch file that is not there is reported, not fatal
    When blokkily is started with a path to no file
    Then the window opens on a new song and the project card says the load failed
