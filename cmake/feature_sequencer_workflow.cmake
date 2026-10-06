# Scenarios: features/sequencer_workflow.feature.
if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_midi_file_tests tests/midi_file_tests.cpp)
    target_link_libraries(blokkily_midi_file_tests PRIVATE blokkily_core)
    add_test(NAME midi_file COMMAND blokkily_midi_file_tests)
    set_tests_properties(midi_file PROPERTIES LABELS "bdd;integration;midi;project" TIMEOUT 30)
endif()
if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/verify/scenario_workflow.cpp)
    if(BLOKKILY_BUILD_TESTS)
        add_test(NAME bdd_sequencer_workflow COMMAND blokkily --verify --scenario workflow
            --clap-fixture $<TARGET_FILE:blokkily_test_clap>
            --screenshot ${CMAKE_BINARY_DIR}/artifacts/sequencer-workflow.png)
        set_tests_properties(bdd_sequencer_workflow PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;midi"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 120)
    endif()
endif()
