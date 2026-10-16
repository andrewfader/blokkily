# Feature fragment: one window opens the files the desktop hands Blokkily
# (features/open_in_running_window.feature, src/app/open_channel.hpp).

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/open_channel.cpp src/app/open_channel.hpp)
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_open_running.cpp)
    # A socket left by a window that is gone, which the gate's window must
    # take over rather than refuse.
    file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/artifacts/open-running)
    add_test(NAME bdd_open_running_stale_socket COMMAND ${CMAKE_COMMAND} -E touch
        ${CMAKE_BINARY_DIR}/artifacts/open-running/open.sock)
    set_tests_properties(bdd_open_running_stale_socket PROPERTIES
        FIXTURES_SETUP blokkily_stale_open_socket LABELS "bdd;desktop")
    add_test(NAME bdd_open_running COMMAND blokkily --verify --scenario open_running
        --project ${BLOKKILY_OPENED_PROJECT}
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/open-running.png)
    set_tests_properties(bdd_open_running PROPERTIES
        FIXTURES_REQUIRED "blokkily_opened_project;blokkily_stale_open_socket"
        LABELS "bdd;e2e;integration;screenshot;project;desktop"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV};BLOKKILY_OPEN_SOCKET=${CMAKE_BINARY_DIR}/artifacts/open-running/open.sock"
        TIMEOUT 120)
endif()
