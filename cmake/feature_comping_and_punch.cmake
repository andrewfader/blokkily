# Feature fragment: comping and punch (features/comping_and_punch.feature).
# Takes of the same bars are a stack of which one plays (comping.hpp); punch
# gates capture to the loop's IN and OUT in the render callback.

target_sources(blokkily_core PRIVATE
    src/audio/comping.cpp
    src/project/records_comping.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/song_model_comping.cpp)
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_comping_tests tests/comping_tests.cpp
        ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES} ${BLOKKILY_SAMPLER_APP_SOURCES})
    target_include_directories(blokkily_comping_tests PRIVATE src src/app tests)
    target_link_libraries(blokkily_comping_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_comping_tests PRIVATE
        BLOKKILY_COMPING_WORK="${CMAKE_BINARY_DIR}/comping-tests")
    foreach(case punch_audio punch_midi comp_passes comp_render records)
        add_test(NAME comping_${case} COMMAND blokkily_comping_tests ${case})
        set_tests_properties(comping_${case} PROPERTIES
            LABELS "bdd;integration;audio;recording;comping"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_comping.cpp)
    add_test(NAME bdd_comping COMMAND blokkily --verify --scenario comping
        --project ${CMAKE_BINARY_DIR}/artifacts/comping/comping.blok
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/comping.png)
    set_tests_properties(bdd_comping PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;audio;recording;comping;project"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 120)
endif()
