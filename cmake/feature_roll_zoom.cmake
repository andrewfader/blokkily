# Feature fragment: the piano roll's zoom and its VELOCITY lane
# (features/editors_are_inputs.feature).

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_roll_zoom.cpp)
    add_test(NAME bdd_roll_zoom COMMAND blokkily --verify --scenario roll_zoom
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/roll-zoom.png)
    set_tests_properties(bdd_roll_zoom PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 90)
endif()
