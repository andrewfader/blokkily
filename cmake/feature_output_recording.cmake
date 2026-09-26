target_sources(blokkily_core PRIVATE src/audio/output_recording.cpp)
if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/app_controller_output.cpp)
endif()
if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_output_recording_tests tests/output_recording_tests.cpp
        ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES} ${BLOKKILY_SAMPLER_APP_SOURCES})
    target_include_directories(blokkily_output_recording_tests PRIVATE src/app)
    target_link_libraries(blokkily_output_recording_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_output_recording_tests PRIVATE
        ${BLOKKILY_EFFECT_FIXTURE_DEFINITIONS}
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_SF2="${BLOKKILY_TEST_SF2}"
        BLOKKILY_OUTPUT_WORK="${CMAKE_BINARY_DIR}/output-recording-tests")
    add_dependencies(blokkily_output_recording_tests blokkily_test_clap blokkily_test_clap_effect)
    foreach(case stems returns live invalid state soundfont_reset)
        add_test(NAME output_recording_${case} COMMAND blokkily_output_recording_tests ${case})
        set_tests_properties(output_recording_${case} PROPERTIES LABELS "bdd;integration;audio;export;recording"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    blokkily_add_realtime_case(output_recording LABELS "audio;recording;export")
endif()
if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_output.cpp)
    add_test(NAME bdd_output_recording COMMAND blokkily --verify --scenario output
        --clap-fixture $<TARGET_FILE:blokkily_test_clap>
        --project ${CMAKE_BINARY_DIR}/artifacts/output/output.blok
        --export ${CMAKE_BINARY_DIR}/artifacts/output/reference.wav
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/output-recording.png)
    set_tests_properties(bdd_output_recording PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;audio;recording;export;clap"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 120)
endif()
