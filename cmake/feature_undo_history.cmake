# Feature fragment: history as a tree (features/undo_history.feature). An edit
# after an undo starts a branch; the HISTORY panel (ui/HistoryPanel.qml, Ctrl+H)
# lists every step and goes to any of them.

if(BLOKKILY_BUILD_GUI)
    set_source_files_properties(ui/HistoryPanel.qml PROPERTIES QT_RESOURCE_ALIAS HistoryPanel.qml)
    qt_target_qml_sources(blokkily QML_FILES ui/HistoryPanel.qml)
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_undo_history.cpp)
    add_test(NAME bdd_undo_history COMMAND blokkily --verify --scenario undo_history
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/undo-history.png)
    set_tests_properties(bdd_undo_history PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 90)
endif()
