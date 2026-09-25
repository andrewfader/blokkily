# Feature fragment: tempo_meter_ui (item 2.1, tempo and meter UI). See docs/plans/daw-program.md.
#
# The tempo lane with its step and ramp handles, the meter menu on the ruler,
# bars drawn as wide as they last, the meter and tempo readouts, patterns
# whose length is not sixteen steps (PatternModel::stepCount, the LEN spinner)
# and a Ctrl-click on the grid that seeks correctly in any meter.
# features/tempo_meter.feature maps each scenario to a check below.

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/app_controller_timebase.cpp)
    # The lane, the meter menu and the LEN spinner are QML types of the
    # window's own module, served beside its qmldir like the rest.
    set(BLOKKILY_TEMPO_METER_QML ui/TempoLane.qml ui/MeterMenu.qml ui/PatternLength.qml)
    foreach(qml_file IN LISTS BLOKKILY_TEMPO_METER_QML)
        get_filename_component(qml_name "${qml_file}" NAME)
        set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
    endforeach()
    qt_target_qml_sources(blokkily QML_FILES ${BLOKKILY_TEMPO_METER_QML})
    # The verify driver's `--scenario tempo-meter` group.
    target_sources(blokkily PRIVATE src/app/verify/scenario_tempo_meter.cpp)
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_tempo_meter_ui_tests tests/tempo_meter_ui_tests.cpp)
    target_include_directories(blokkily_tempo_meter_ui_tests PRIVATE tests)
    target_link_libraries(blokkily_tempo_meter_ui_tests PRIVATE blokkily_core)
    target_compile_options(blokkily_tempo_meter_ui_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    target_compile_definitions(blokkily_tempo_meter_ui_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>")
    add_dependencies(blokkily_tempo_meter_ui_tests blokkily_test_clap)
    foreach(tempo_meter_case pattern_with_length pattern_length_saved)
        add_test(NAME tempo_meter_${tempo_meter_case}
            COMMAND blokkily_tempo_meter_ui_tests ${tempo_meter_case})
        set_tests_properties(tempo_meter_${tempo_meter_case} PROPERTIES
            LABELS "bdd;unit;timebase;pattern;project"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    add_test(NAME tempo_meter_fourteen_steps_in_seven_eight
        COMMAND blokkily_tempo_meter_ui_tests fourteen_steps_in_seven_eight)
    set_tests_properties(tempo_meter_fourteen_steps_in_seven_eight PROPERTIES
        LABELS "bdd;integration;audio;clap;timebase;pattern"
        ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    # The tempo lane, the ruler's meter menu, proportional bars, fourteen-step
    # patterns in every editor, seeking, a take and the pump in a 7/8 bar, save,
    # load and undo, and a screenshot of it all.
    add_test(NAME bdd_tempo_meter
        COMMAND blokkily --verify --scenario tempo-meter
            --clap-fixture $<TARGET_FILE:blokkily_test_clap>
            --project ${CMAKE_BINARY_DIR}/artifacts/tempo-meter.blok
            --screenshot ${CMAKE_BINARY_DIR}/artifacts/tempo-meter.png)
    set_tests_properties(bdd_tempo_meter PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap;project;timebase;recording;midi;song"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
        TIMEOUT 120)
endif()
