# Feature fragment: chord_velocity (item 1.6, chord velocity, the note-merging fix). See docs/plans/daw-program.md.
#
# Chords carry a velocity and a length per voice, so a take merged onto a step
# keeps how hard and how long each key was played. The model, scheduler and
# take recorder changes live in files blokkily_core already builds; this
# fragment adds the additive voicevel/voicelen records, the scenario tests of
# features/chord_velocity.feature, the real-time case and the GUI gate.

target_sources(blokkily_core PRIVATE src/project/records_chord_velocity.cpp)

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_chord_velocity_tests tests/chord_velocity_tests.cpp)
    target_include_directories(blokkily_chord_velocity_tests PRIVATE tests)
    target_link_libraries(blokkily_chord_velocity_tests PRIVATE blokkily_core)
    target_compile_options(blokkily_chord_velocity_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    target_compile_definitions(blokkily_chord_velocity_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>")
    if(BLOKKILY_TEST_SF2)
        target_compile_definitions(blokkily_chord_velocity_tests PRIVATE
            BLOKKILY_TEST_SF2_PATH="${BLOKKILY_TEST_SF2}")
    endif()
    add_dependencies(blokkily_chord_velocity_tests blokkily_test_clap)
    foreach(chord_case write_played scheduler_clap legacy_chord soundfont project)
        add_test(NAME chord_velocity_${chord_case}
            COMMAND blokkily_chord_velocity_tests ${chord_case})
        set_tests_properties(chord_velocity_${chord_case} PROPERTIES
            LABELS "bdd;unit;chords;velocity"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}"
            SKIP_RETURN_CODE 77
            TIMEOUT 60)
    endforeach()
    set_property(TEST chord_velocity_write_played APPEND PROPERTY LABELS "regression;recording")
    set_property(TEST chord_velocity_scheduler_clap APPEND PROPERTY LABELS "integration;audio;clap")
    set_property(TEST chord_velocity_legacy_chord APPEND PROPERTY LABELS "integration;audio;clap;project")
    set_property(TEST chord_velocity_soundfont APPEND PROPERTY LABELS "integration;audio;soundfont")
    set_property(TEST chord_velocity_project APPEND PROPERTY LABELS "project")

    # A chord with per-voice velocities and lengths plays through the engine
    # and the CLAP fixture without the render callback allocating.
    blokkily_add_realtime_case(chord_velocity LABELS "audio;clap;chords;velocity")
endif()

if(BLOKKILY_BUILD_GUI)
    # The verify driver's `--scenario chords` group.
    target_sources(blokkily PRIVATE src/app/verify/scenario_chords.cpp)
    if(BLOKKILY_BUILD_TESTS)
        # Two keys recorded onto one step at velocities 40 and 120: the chord
        # keeps both, the inspector's per-voice bar edits one of them, and the
        # CLAP fixture's rendered level proves it; undo, redo, save and load.
        add_test(NAME bdd_chord_velocity
            COMMAND blokkily --verify --scenario chords
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --project ${CMAKE_BINARY_DIR}/artifacts/chord-velocity.blok
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/chord-velocity.png)
        set_tests_properties(bdd_chord_velocity PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;midi;recording;clap;project;chords;velocity"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 120)
    endif()
endif()
