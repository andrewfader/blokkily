# Feature fragment: loading_progress. See features/loading_progress.feature.
#
# What the window shows while a song opens, its instruments load or it is
# exported (src/app/load_progress.cpp, ui/LoadingOverlay.qml), and the strip
# along the transport bar for the plugin scan and background audio work.

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        src/app/load_progress.cpp
        src/app/load_progress.hpp
        src/app/verify/scenario_loading.cpp)
    set(BLOKKILY_LOADING_QML ui/LoadingOverlay.qml)
    foreach(qml_file IN LISTS BLOKKILY_LOADING_QML)
        get_filename_component(qml_name "${qml_file}" NAME)
        set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
    endforeach()
    qt_target_qml_sources(blokkily QML_FILES ${BLOKKILY_LOADING_QML})
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    # A song opened and exported in the rendered window, every frame each
    # draws read back; input and an agent's request held off while one runs;
    # the export compared, read back from disk, with the same export made with
    # its progress hidden; the scan strip; a cached scan listed in one piece;
    # the closed tour costing nothing. The screenshot is the window half way
    # through the export.
    add_test(NAME bdd_loading_progress
        COMMAND blokkily --verify --scenario loading
            --clap-fixture $<TARGET_FILE:blokkily_test_clap>
            --soundfont-fixture ${BLOKKILY_TEST_SF2}
            --scan-fixture ${BLOKKILY_CLAP_FIXTURE_DIR}
            --mcp-socket blokkily-loading-gate
            --project ${CMAKE_BINARY_DIR}/artifacts/loading.blok
            --export ${CMAKE_BINARY_DIR}/artifacts/loading-export.wav
            --screenshot ${CMAKE_BINARY_DIR}/artifacts/loading-progress.png)
    set_tests_properties(bdd_loading_progress PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap;soundfont;project;export;scan;mcp;loading"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV};BLOKKILY_SCAN_TIMEOUT_MS=1500;BLOKKILY_SCAN_CACHE=${CMAKE_BINARY_DIR}/artifacts/loading-scan-cache.txt"
        TIMEOUT 120)
endif()
