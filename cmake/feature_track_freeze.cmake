# Feature fragment: track freeze (features/track_freeze.feature). A frozen
# track's instrument and inserts are rendered pre-fader to a freeze clip and
# left out of the graph.

target_sources(blokkily_core PRIVATE src/project/records_freeze.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/app_controller_freeze.cpp)
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_freeze_tests tests/freeze_tests.cpp
        ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES} ${BLOKKILY_SAMPLER_APP_SOURCES})
    target_include_directories(blokkily_freeze_tests PRIVATE src src/app tests)
    target_link_libraries(blokkily_freeze_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_freeze_tests PRIVATE
        ${BLOKKILY_EFFECT_FIXTURE_DEFINITIONS}
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_FREEZE_WORK="${CMAKE_BINARY_DIR}/freeze-tests")
    add_dependencies(blokkily_freeze_tests blokkily_test_clap blokkily_test_clap_effect)
    foreach(case sounds_the_same other_clips records)
        add_test(NAME freeze_${case} COMMAND blokkily_freeze_tests ${case})
        set_tests_properties(freeze_${case} PROPERTIES
            LABELS "bdd;integration;audio;clap;mixer;freeze"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_freeze.cpp)
    add_test(NAME bdd_freeze COMMAND blokkily --verify --scenario freeze
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --project ${CMAKE_BINARY_DIR}/artifacts/freeze/freeze.blok
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/freeze.png)
    set_tests_properties(bdd_freeze PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;clap;mixer;freeze;project;export"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 120)
endif()
