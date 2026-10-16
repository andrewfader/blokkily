# Feature fragment: editing per-note expression and poly pressure in the
# piano roll's controller lane (features/note_expression_editing.feature).

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_note_expression.cpp)
    add_test(NAME bdd_note_expression COMMAND blokkily --verify --scenario note_expression
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/note-expression.png)
    set_tests_properties(bdd_note_expression PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap;midi;expression"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 120)
endif()
