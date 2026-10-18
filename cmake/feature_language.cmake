# Feature fragment: the window in another language (features/language.feature).
# Every label the window shows is a qsTr; i18n/blokkily_<locale>.ts holds a
# translation, compiled into the program under :/i18n. `cmake --build build
# --target update_translations` gathers new strings into the .ts files.

if(BLOKKILY_BUILD_GUI)
    find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
    target_sources(blokkily PRIVATE src/app/language.cpp src/app/language.hpp)
    qt_add_translations(blokkily
        TS_FILES i18n/blokkily_de.ts
        RESOURCE_PREFIX /i18n
        LUPDATE_OPTIONS -no-obsolete -locations none)
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_language.cpp)
    add_test(NAME bdd_language COMMAND blokkily --verify --scenario language
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/language.png)
    set_tests_properties(bdd_language PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 120)
endif()
