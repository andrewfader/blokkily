# Feature fragment: opening a .blok from the desktop
# (features/open_from_desktop.feature). The desktop entry claims the type
# resources/blokkily-mime.xml defines and passes the file (Exec=blokkily %f);
# gui_main.cpp opens a project named on the command line at launch.

if(BLOKKILY_BUILD_TESTS)
    find_program(BLOKKILY_UPDATE_MIME update-mime-database REQUIRED)
    find_program(BLOKKILY_DESKTOP_VALIDATE desktop-file-validate REQUIRED)
    add_test(NAME desktop_integration COMMAND ${CMAKE_COMMAND}
        -DSOURCE=${CMAKE_SOURCE_DIR}
        -DWORK=${CMAKE_BINARY_DIR}/desktop-integration
        -DUPDATE_MIME=${BLOKKILY_UPDATE_MIME}
        -DVALIDATE=${BLOKKILY_DESKTOP_VALIDATE}
        -P ${CMAKE_SOURCE_DIR}/tests/desktop_integration.cmake)
    set_tests_properties(desktop_integration PROPERTIES
        LABELS "unit;bdd;integration;project;desktop" TIMEOUT 30)
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_open_file.cpp)
    # A space in the name, and a file:// URL with it escaped, as a file
    # manager may hand either over.
    set(BLOKKILY_OPENED_PROJECT "${CMAKE_BINARY_DIR}/artifacts/open-file/opened song.blok")
    add_test(NAME bdd_open_file_setup COMMAND blokkily --verify --scenario open_file_setup
        --project ${BLOKKILY_OPENED_PROJECT})
    set_tests_properties(bdd_open_file_setup PROPERTIES
        FIXTURES_SETUP blokkily_opened_project
        LABELS "bdd;e2e;integration;project;desktop"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 60)
    add_test(NAME bdd_open_file_path COMMAND blokkily --verify --scenario open_file
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/open-file.png
        ${BLOKKILY_OPENED_PROJECT})
    add_test(NAME bdd_open_file_url COMMAND blokkily --verify --scenario open_file
        "file://${CMAKE_BINARY_DIR}/artifacts/open-file/opened%20song.blok")
    add_test(NAME bdd_open_file_missing COMMAND blokkily --verify --scenario open_file_missing
        ${CMAKE_BINARY_DIR}/artifacts/open-file/not-there.blok)
    set_tests_properties(bdd_open_file_path bdd_open_file_url PROPERTIES
        FIXTURES_REQUIRED blokkily_opened_project)
    set_tests_properties(bdd_open_file_path PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;project;desktop")
    set_tests_properties(bdd_open_file_url bdd_open_file_missing PROPERTIES
        LABELS "bdd;e2e;integration;project;desktop")
    set_tests_properties(bdd_open_file_path bdd_open_file_url bdd_open_file_missing PROPERTIES
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 60)
endif()
