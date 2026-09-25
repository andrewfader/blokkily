# Feature fragment: metronome (item 3.7, metronome and count-in). See docs/plans/daw-program.md.
#
# Included from CMakeLists.txt after every core target exists, in a fixed
# order. This file owns the feature's sources (target_sources on blokkily_core
# or blokkily), its tests, labels and CTest gates. Headless tests use
# ${BLOKKILY_HEADLESS_TEST_ENV} and offscreen GUI gates use
# ${BLOKKILY_OFFSCREEN_GATE_ENV} in their ENVIRONMENT. Register tests only
# inside if(BLOKKILY_BUILD_TESTS), and GUI gates only inside
# if(BLOKKILY_BUILD_GUI).
#
# Empty until the owning work item lands.
