# Feature fragment: several notes chosen in the piano roll and edited
# together (features/note_selection.feature), through the rendered window.

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_note_selection.cpp)
    add_test(NAME bdd_note_selection COMMAND blokkily --verify --scenario note_selection
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/note-selection.png)
    set_tests_properties(bdd_note_selection PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 90)
endif()
