# Feature fragment: MIDI learn (features/midi_learn.feature). The MIDI
# input's controller feed, the song's bindings and their `midi_learn`
# record, and the learn controls of the track menu and macro cards.

target_sources(blokkily_core PRIVATE src/project/records_midi_learn.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/app_controller_midi_learn.cpp)
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_midi_learn_tests tests/midi_learn_tests.cpp)
    target_link_libraries(blokkily_midi_learn_tests PRIVATE blokkily_core)
    foreach(case feed records track_removal)
        add_test(NAME midi_learn_${case} COMMAND blokkily_midi_learn_tests ${case})
        set_tests_properties(midi_learn_${case} PROPERTIES
            LABELS "unit;bdd;midi;project" ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 30)
    endforeach()
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_midi_learn.cpp)
    add_test(NAME bdd_midi_learn COMMAND blokkily --verify --scenario midi_learn
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --project ${CMAKE_BINARY_DIR}/artifacts/midi-learn.blok
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/midi-learn.png)
    set_tests_properties(bdd_midi_learn PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;midi;mixer;clap;project"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 120)
endif()
