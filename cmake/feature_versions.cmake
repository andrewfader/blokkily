# Feature fragment: versions. The song's notes, and versions of the song saved
# along the way, as the VERSIONS and NOTES views (features/versions.feature).

target_sources(blokkily_core PRIVATE src/project/records_versions.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        src/app/song_model_versions.cpp
        src/app/app_controller_versions.cpp
        src/app/verify/scenario_versions.cpp)
    set(BLOKKILY_VERSIONS_QML ui/VersionsPanel.qml ui/NotesPanel.qml)
    foreach(qml_file IN LISTS BLOKKILY_VERSIONS_QML)
        get_filename_component(qml_name "${qml_file}" NAME)
        set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
    endforeach()
    qt_target_qml_sources(blokkily QML_FILES ${BLOKKILY_VERSIONS_QML})
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_versions_tests tests/versions_tests.cpp)
    target_link_libraries(blokkily_versions_tests PRIVATE blokkily_core)
    target_compile_options(blokkily_versions_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    foreach(versions_case project unchanged malformed)
        add_test(NAME versions_${versions_case} COMMAND blokkily_versions_tests ${versions_case})
        set_tests_properties(versions_${versions_case} PROPERTIES
            LABELS "bdd;unit;project;versions" TIMEOUT 60)
    endforeach()
    if(BLOKKILY_BUILD_GUI)
        # NOTES and VERSIONS in the rendered window: typed notes, versions
        # saved and restored and heard through the production callback, undo,
        # a save and a load, and a two-press delete.
        add_test(NAME bdd_versions
            COMMAND blokkily --verify --scenario versions
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --project ${CMAKE_BINARY_DIR}/artifacts/versions.blok
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/versions.png)
        set_tests_properties(bdd_versions PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;project;versions;clap"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 120)
    endif()
endif()
