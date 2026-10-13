# Feature fragment: midi_timing. Played MIDI keeps its timing to the sample:
# each message is stamped as it arrives and placed in the next device block
# where it arrived in the period before it (features/midi_timing.feature).
# The engine change lives in files blokkily_core already builds; this
# fragment adds the scenario tests.

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_midi_timing_tests tests/midi_timing_tests.cpp)
    target_include_directories(blokkily_midi_timing_tests PRIVATE tests)
    target_link_libraries(blokkily_midi_timing_tests PRIVATE blokkily_core)
    target_compile_options(blokkily_midi_timing_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    target_compile_definitions(blokkily_midi_timing_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>")
    add_dependencies(blokkily_midi_timing_tests blokkily_test_clap)
    foreach(timing_case placed spacing chunks recorded unstamped)
        add_test(NAME midi_timing_${timing_case}
            COMMAND blokkily_midi_timing_tests ${timing_case})
        set_tests_properties(midi_timing_${timing_case} PROPERTIES
            LABELS "bdd;unit;integration;midi;audio;clap;timing"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}"
            TIMEOUT 60)
    endforeach()
    set_property(TEST midi_timing_recorded APPEND PROPERTY LABELS "recording")
endif()

if(BLOKKILY_BUILD_TESTS)
    # Placing, sorting and handing out stamped input across chunks, and
    # capturing it, costs the render callback no allocation.
    blokkily_add_realtime_case(midi_timing LABELS "audio;clap;midi;timing;recording")
endif()
