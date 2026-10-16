# Feature fragment: quantize and humanize the open part
# (features/quantize_humanize.feature, blokkily/model/groove.hpp).

target_sources(blokkily_core PRIVATE src/model/groove.cpp)

if(BLOKKILY_BUILD_GUI)
    set(BLOKKILY_GROOVE_QML_FILES ui/PatternGroove.qml)
    foreach(qml_file IN LISTS BLOKKILY_GROOVE_QML_FILES)
        get_filename_component(qml_name "${qml_file}" NAME)
        set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
    endforeach()
    qt_target_qml_sources(blokkily QML_FILES ${BLOKKILY_GROOVE_QML_FILES})
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_groove_tests tests/groove_tests.cpp
        ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES} ${BLOKKILY_SAMPLER_APP_SOURCES})
    target_include_directories(blokkily_groove_tests PRIVATE src/app)
    target_link_libraries(blokkily_groove_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_groove_tests PRIVATE
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>")
    add_dependencies(blokkily_groove_tests blokkily_test_clap)
    foreach(case quantize_full quantize_strength quantize_collisions humanize render)
        add_test(NAME groove_${case} COMMAND blokkily_groove_tests ${case})
        set_tests_properties(groove_${case} PROPERTIES
            LABELS "unit;bdd;integration;sequencer"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_groove.cpp)
    add_test(NAME bdd_groove COMMAND blokkily --verify --scenario groove
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/groove.png)
    set_tests_properties(bdd_groove PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;sequencer"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 60)
endif()
