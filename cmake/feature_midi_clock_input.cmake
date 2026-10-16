# Feature fragment: following an external MIDI clock
# (features/midi_clock_input.feature): the input's clock feed, the follower,
# the engine's tempo override and the SYNC row.

target_sources(blokkily_core PRIVATE src/midi/clock_follower.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/app_controller_clock.cpp)
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_midi_clock_tests tests/midi_clock_tests.cpp
        ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES} ${BLOKKILY_SAMPLER_APP_SOURCES})
    target_include_directories(blokkily_midi_clock_tests PRIVATE src/app)
    target_link_libraries(blokkily_midi_clock_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_midi_clock_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>")
    add_dependencies(blokkily_midi_clock_tests blokkily_test_clap)
    foreach(case follower_tempo follower_transport input_feed engine_override)
        add_test(NAME midi_clock_${case} COMMAND blokkily_midi_clock_tests ${case})
        set_tests_properties(midi_clock_${case} PROPERTIES
            LABELS "unit;bdd;midi;sync" ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_midi_clock.cpp)
    add_test(NAME bdd_midi_clock COMMAND blokkily --verify --scenario midi_clock
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/midi-clock.png)
    set_tests_properties(bdd_midi_clock PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;midi;sync;clap"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 120)
endif()
