# Feature fragment: several songs open at once, each in a window of its own
# (features/multiple_windows.feature, src/app/app_controller_windows.cpp).
# Every window is a Blokkily process; + WINDOW and OPEN NEW start one.

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/app_controller_windows.cpp)
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE
        src/app/verify/scenario_windows.cpp
        src/app/verify/window_report.hpp)
    # The windows the gate opens are real processes started by the window's
    # own code; each renders multiple-windows-opened.png and reports back.
    add_test(NAME bdd_multiple_windows COMMAND blokkily --verify --scenario windows
        --project ${BLOKKILY_OPENED_PROJECT}
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/multiple-windows.png)
    set_tests_properties(bdd_multiple_windows PROPERTIES
        FIXTURES_REQUIRED blokkily_opened_project
        LABELS "bdd;e2e;integration;screenshot;project;desktop"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV};BLOKKILY_MCP=0"
        TIMEOUT 180)
endif()
