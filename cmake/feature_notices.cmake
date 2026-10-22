# Feature fragment: notices (features/notices.feature). What the window has
# to tell the producer - a failure, or work that finished - is said over the
# editors (src/app/app_controller_notices.cpp, ui/NoticeStack.qml), not in a
# status line at the foot of a bar that hides.

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/app_controller_notices.cpp)
    set_source_files_properties(ui/NoticeStack.qml PROPERTIES QT_RESOURCE_ALIAS NoticeStack.qml)
    qt_target_qml_sources(blokkily QML_FILES ui/NoticeStack.qml)
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_notices.cpp)
    add_test(NAME bdd_notices COMMAND blokkily --verify --scenario notices
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/notices.png)
    set_tests_properties(bdd_notices PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap;project"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 90)
endif()
