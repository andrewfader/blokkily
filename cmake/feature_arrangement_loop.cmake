# Feature fragment: arrangement loop. Playback wraps between IN and OUT on
# the arrangement; an export still renders the whole song.
# features/arrangement_loop.feature maps each scenario to a check below.

target_sources(blokkily_core PRIVATE
    src/project/records_loop.cpp
    src/audio/song_engine_loop.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        src/app/song_model_loop.cpp
        src/app/verify/scenario_loop.cpp)
    set_source_files_properties(ui/LoopControls.qml PROPERTIES
        QT_RESOURCE_ALIAS LoopControls.qml)
    qt_target_qml_sources(blokkily QML_FILES ui/LoopControls.qml)
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_loop_tests tests/loop_tests.cpp)
    target_include_directories(blokkily_loop_tests PRIVATE tests)
    target_link_libraries(blokkily_loop_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_loop_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>")
    target_compile_options(blokkily_loop_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_loop_tests blokkily_test_clap)
    foreach(loop_case wraps_playback off_plays_the_song turned_on_after_a_lap mixer_live)
        add_test(NAME loop_${loop_case} COMMAND blokkily_loop_tests ${loop_case})
        set_tests_properties(loop_${loop_case} PROPERTIES
            LABELS "bdd;integration;audio;engine;clap;arrangement_loop"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    set_property(TEST loop_mixer_live APPEND PROPERTY LABELS "mixer")
    add_test(NAME loop_bounce_is_the_arrangement
        COMMAND blokkily_loop_tests bounce_is_the_arrangement)
    set_tests_properties(loop_bounce_is_the_arrangement PROPERTIES
        LABELS "bdd;integration;audio;clap;engine;export;arrangement_loop"
        ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    foreach(loop_case project malformed)
        add_test(NAME loop_${loop_case} COMMAND blokkily_loop_tests ${loop_case})
        set_tests_properties(loop_${loop_case} PROPERTIES
            LABELS "unit;project;arrangement_loop"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()

    blokkily_add_realtime_case(arrangement_loop LABELS "audio;clap;engine;arrangement_loop")
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    add_test(NAME bdd_arrangement_loop
        COMMAND blokkily --verify --scenario arrangement-loop
            --clap-fixture $<TARGET_FILE:blokkily_test_clap>
            --project ${CMAKE_BINARY_DIR}/artifacts/arrangement-loop.blok
            --export ${CMAKE_BINARY_DIR}/artifacts/arrangement-loop.wav
            --screenshot ${CMAKE_BINARY_DIR}/artifacts/arrangement-loop.png)
    set_tests_properties(bdd_arrangement_loop PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap;project;export;arrangement_loop"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
        TIMEOUT 120)
endif()
