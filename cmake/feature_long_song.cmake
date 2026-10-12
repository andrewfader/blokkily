# Feature fragment: long_song. See features/session_workflow.feature, "A long
# song stays light while it is mixed".
#
# A four-minute, eight-track arrangement in the real window, mixed: memory
# must stay where it was and the arrangement must be drawn as the song is.

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/verify/scenario_long_song.cpp)
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    add_test(NAME bdd_long_song
        COMMAND blokkily --verify --scenario long_song
            --clap-fixture $<TARGET_FILE:blokkily_test_clap>
            --screenshot ${CMAKE_BINARY_DIR}/artifacts/long-song.png)
    set_tests_properties(bdd_long_song PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;mixer;song;clap"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
        TIMEOUT 120)
endif()
