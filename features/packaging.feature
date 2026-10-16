Feature: Packaging as an AppImage and a Flatpak
  Installing Blokkily also installs AppStream metadata
  (share/metainfo/dev.blokkily.Blokkily.metainfo.xml), which software
  centres read. packaging/appimage/build-appimage.sh turns a Release build
  into Blokkily-<version>-<arch>.AppImage, and
  packaging/flatpak/dev.blokkily.Blokkily.json builds the Flatpak
  dev.blokkily.Blokkily. The suite checks that they agree with the build;
  building the packages themselves needs linuxdeploy or flatpak-builder and a
  network connection, so it is not part of the suite.

  # packaging_metadata
  Scenario: The metadata agrees with the build
    Given a configured build
    Then the AppStream metadata is well-formed XML naming dev.blokkily.Blokkily, launched by blokkily.desktop, with a release of the project's version
    And the desktop entry it launches is valid
    And the Flatpak manifest parses, builds the app dev.blokkily.Blokkily running blokkily, and has a blokkily module
    And the AppImage script is valid shell
