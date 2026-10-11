# Scenarios: features/sequencer_fidelity.feature. What a tracker or DAW user
# expects of the sequencer, the engine and the files, proved in rendered audio,
# scheduled events and bytes read back.
if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_sequencer_fidelity_tests tests/sequencer_fidelity_tests.cpp)
    target_include_directories(blokkily_sequencer_fidelity_tests PRIVATE tests)
    target_link_libraries(blokkily_sequencer_fidelity_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_sequencer_fidelity_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_ARTIFACTS="${CMAKE_BINARY_DIR}/artifacts")
    target_compile_options(blokkily_sequencer_fidelity_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_sequencer_fidelity_tests blokkily_test_clap)
    foreach(fidelity_case
            loop_numbers_continue
            inversion_octaves
            roll_becomes_ratchet
            release_pairs_newest_press
            pickup_note_exported
            project_numbers_locale_free)
        add_test(NAME sequencer_fidelity_${fidelity_case}
            COMMAND blokkily_sequencer_fidelity_tests ${fidelity_case})
        set_tests_properties(sequencer_fidelity_${fidelity_case} PROPERTIES
            LABELS "unit;bdd;regression;project;midi"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    foreach(fidelity_case
            overlap_hands_over
            playhead_keeps_place
            fader_move_ramps
            bounce_tail_holds_end)
        add_test(NAME sequencer_fidelity_${fidelity_case}
            COMMAND blokkily_sequencer_fidelity_tests ${fidelity_case})
        set_tests_properties(sequencer_fidelity_${fidelity_case} PROPERTIES
            LABELS "bdd;integration;regression;audio;clap;engine;mixer;export"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
endif()
