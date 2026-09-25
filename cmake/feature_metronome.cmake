# Feature fragment: metronome (item 3.7, metronome and count-in). See docs/plans/daw-program.md.
#
# A click that follows the tempo and meter maps, on its own path into the
# output (no track, solo or mute reaches it), left out of a bounce unless the
# export asks for it; and a count-in of 0 to 4 bars before a recording, during
# which the song position does not move and nothing is captured.
# features/metronome.feature maps each scenario to a check below.

# The engine's click and count-in, and the project record.
target_sources(blokkily_core PRIVATE
    src/audio/engine/engine_metronome.cpp
    src/audio/song_engine_metronome.cpp
    src/project/records_metronome.cpp)

if(BLOKKILY_BUILD_GUI)
    # The settings on the song model, the controller's count-in state, the
    # transport's controls, and the verify driver's `--scenario metronome`.
    target_sources(blokkily PRIVATE
        src/app/song_model_metronome.cpp
        src/app/app_controller_metronome.cpp
        src/app/verify/scenario_metronome.cpp)
    set_source_files_properties(ui/MetronomeControls.qml PROPERTIES
        QT_RESOURCE_ALIAS MetronomeControls.qml)
    qt_target_qml_sources(blokkily QML_FILES ui/MetronomeControls.qml)
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_metronome_tests tests/metronome_tests.cpp)
    target_include_directories(blokkily_metronome_tests PRIVATE tests)
    target_link_libraries(blokkily_metronome_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_metronome_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>")
    target_compile_options(blokkily_metronome_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_metronome_tests blokkily_test_clap)
    foreach(metronome_case
            clicks_on_tempo_step
            clicks_in_seven_eight
            click_level_and_switch)
        add_test(NAME metronome_${metronome_case}
            COMMAND blokkily_metronome_tests ${metronome_case})
        set_tests_properties(metronome_${metronome_case} PROPERTIES
            LABELS "bdd;integration;audio;engine;timebase;metronome"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    add_test(NAME metronome_click_ignores_solo_and_mute
        COMMAND blokkily_metronome_tests click_ignores_solo_and_mute)
    set_tests_properties(metronome_click_ignores_solo_and_mute PROPERTIES
        LABELS "bdd;integration;audio;clap;engine;mixer;metronome"
        ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    add_test(NAME metronome_bounce_click_option
        COMMAND blokkily_metronome_tests bounce_click_option)
    set_tests_properties(metronome_bounce_click_option PROPERTIES
        LABELS "bdd;integration;audio;clap;engine;export;metronome"
        ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    foreach(metronome_case count_in_bars count_in_recording)
        add_test(NAME metronome_${metronome_case}
            COMMAND blokkily_metronome_tests ${metronome_case})
        set_tests_properties(metronome_${metronome_case} PROPERTIES
            LABELS "bdd;integration;audio;clap;engine;recording;midi;metronome"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    add_test(NAME metronome_project_records
        COMMAND blokkily_metronome_tests project_records)
    set_tests_properties(metronome_project_records PROPERTIES
        LABELS "unit;project;metronome"
        ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)

    # The click and the count-in on the render thread.
    blokkily_add_realtime_case(metronome LABELS "audio;clap;engine;midi;metronome")
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    # The transport's click, count-in and level controls, driven through the
    # rendered window; the click heard from the production callback; a
    # count-in with a take recorded into it; the bounce option read back;
    # save, load and undo; and a screenshot.
    add_test(NAME bdd_metronome
        COMMAND blokkily --verify --scenario metronome
            --clap-fixture $<TARGET_FILE:blokkily_test_clap>
            --project ${CMAKE_BINARY_DIR}/artifacts/metronome.blok
            --export ${CMAKE_BINARY_DIR}/artifacts/metronome.wav
            --screenshot ${CMAKE_BINARY_DIR}/artifacts/metronome.png)
    set_tests_properties(bdd_metronome PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap;project;export;recording;metronome"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
        TIMEOUT 120)
endif()
