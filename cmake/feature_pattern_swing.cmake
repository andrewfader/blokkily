# Feature fragment: pattern swing. Even sixteenths are delayed when the
# pattern plays; the editors keep the straight grid.
# features/pattern_swing.feature maps each scenario to a check below.

target_sources(blokkily_core PRIVATE src/project/records_swing.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        src/app/song_model_swing.cpp
        src/app/verify/scenario_swing.cpp)
    set_source_files_properties(ui/PatternSwing.qml PROPERTIES
        QT_RESOURCE_ALIAS PatternSwing.qml)
    qt_target_qml_sources(blokkily QML_FILES ui/PatternSwing.qml)
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_swing_tests tests/swing_tests.cpp)
    target_include_directories(blokkily_swing_tests PRIVATE tests)
    target_link_libraries(blokkily_swing_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_swing_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>")
    target_compile_options(blokkily_swing_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_swing_tests blokkily_test_clap)
    foreach(swing_case scheduler_delays_offbeats off_is_straight heard)
        add_test(NAME swing_${swing_case} COMMAND blokkily_swing_tests ${swing_case})
        set_tests_properties(swing_${swing_case} PROPERTIES
            LABELS "bdd;integration;audio;engine;clap;pattern_swing"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    set_property(TEST swing_scheduler_delays_offbeats APPEND PROPERTY LABELS "unit")
    set_property(TEST swing_heard APPEND PROPERTY LABELS "export")
    foreach(swing_case project malformed)
        add_test(NAME swing_${swing_case} COMMAND blokkily_swing_tests ${swing_case})
        set_tests_properties(swing_${swing_case} PROPERTIES
            LABELS "unit;project;pattern_swing"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()

    blokkily_add_realtime_case(pattern_swing LABELS "audio;clap;engine;pattern_swing")
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    add_test(NAME bdd_pattern_swing
        COMMAND blokkily --verify --scenario pattern-swing
            --clap-fixture $<TARGET_FILE:blokkily_test_clap>
            --project ${CMAKE_BINARY_DIR}/artifacts/pattern-swing.blok
            --export ${CMAKE_BINARY_DIR}/artifacts/pattern-swing.wav
            --screenshot ${CMAKE_BINARY_DIR}/artifacts/pattern-swing.png)
    set_tests_properties(bdd_pattern_swing PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap;project;export;pattern_swing"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
        TIMEOUT 120)
endif()
