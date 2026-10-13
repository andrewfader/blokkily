# Feature fragment: midi_output. A track's instrument can be outboard gear
# reached through a MIDI output port, and the song can send MIDI clock
# (features/midi_output.feature). MidiOutput and MidiOutInstrument live in
# blokkily_core; this fragment adds the scenario tests.

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_midi_output_tests tests/midi_output_tests.cpp)
    target_include_directories(blokkily_midi_output_tests PRIVATE tests)
    target_link_libraries(blokkily_midi_output_tests PRIVATE blokkily_core)
    target_compile_options(blokkily_midi_output_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    foreach(output_case notes controllers live stop clock bounce state)
        add_test(NAME midi_output_${output_case}
            COMMAND blokkily_midi_output_tests ${output_case})
        set_tests_properties(midi_output_${output_case} PROPERTIES
            LABELS "bdd;unit;integration;midi;output"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}"
            TIMEOUT 60)
    endforeach()
    set_property(TEST midi_output_bounce APPEND PROPERTY LABELS "export")
    set_property(TEST midi_output_state APPEND PROPERTY LABELS "project")
endif()

if(BLOKKILY_BUILD_TESTS)
    blokkily_add_realtime_case(midi_output LABELS "midi;output;integration")
endif()

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/app_controller_midi_out.cpp)
endif()

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/verify/scenario_midi_output.cpp)
    if(BLOKKILY_BUILD_TESTS)
        # Track 0 given the deterministic MIDI output on channel 10 from the
        # rendered MIDI OUT panel, and the clock sent there too; what the
        # output receives as the song plays; undo and redo.
        add_test(NAME bdd_midi_output
            COMMAND blokkily --verify --scenario midi_output
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/midi-output.png)
        set_tests_properties(bdd_midi_output PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;midi;output;clap"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 120)
    endif()
endif()
