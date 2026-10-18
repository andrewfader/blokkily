# Feature fragment: the SETTINGS view (features/audio_settings.feature) and the
# window's layout - auto-hiding side bars, movable and sizable panels
# (features/window_layout.feature).

if(BLOKKILY_BUILD_GUI)
    set(BLOKKILY_WINDOW_LAYOUT_QML ui/SettingsPanel.qml ui/SideBar.qml ui/PanelDock.qml)
    foreach(qml_file IN LISTS BLOKKILY_WINDOW_LAYOUT_QML)
        get_filename_component(qml_name "${qml_file}" NAME)
        set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
    endforeach()
    qt_target_qml_sources(blokkily QML_FILES ${BLOKKILY_WINDOW_LAYOUT_QML})
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE
        src/app/verify/scenario_settings.cpp
        src/app/verify/scenario_window_layout.cpp)
    # The SETTINGS view in the rendered window: its pickers, and another rate
    # and block reaching the device and heard through the production callback.
    add_test(NAME bdd_settings COMMAND blokkily --verify --scenario settings
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/settings.png)
    set_tests_properties(bdd_settings PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;audio;clap"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 90)
    # The side bars as they ship (auto-hiding), and the panels moved and
    # sized, all through pointer and key events at the window.
    add_test(NAME bdd_window_layout COMMAND blokkily --verify --scenario window_layout --auto-hide
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/window-layout.png)
    set_tests_properties(bdd_window_layout PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 90)
endif()
