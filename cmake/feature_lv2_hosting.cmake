# Feature fragment: LV2 hosting (features/lv2_hosting.feature). LV2 plugins
# are hosted through JUCE's LV2 format by the same adapter as VST3
# (vst3_instance.hpp), scanned out of process like the other formats. The
# suite builds a real .lv2 bundle with JUCE as its fixture.

if(BLOKKILY_BUILD_TESTS)
    juce_add_plugin(blokkily_test_lv2
        COMPANY_NAME Blokkily
        PLUGIN_MANUFACTURER_CODE Blok
        PLUGIN_CODE Blv2
        FORMATS LV2
        LV2URI "urn:blokkily:test-lv2"
        PRODUCT_NAME "Blokkily Test LV2"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE)
    target_sources(blokkily_test_lv2 PRIVATE tests/fixtures/test_lv2.cpp)
    target_link_libraries(blokkily_test_lv2 PRIVATE
        juce::juce_audio_utils juce::juce_recommended_config_flags)
    # JUCE's LV2 manifest helper takes CommandLineToArgvW from windows.h,
    # which leaves shellapi.h out under the WIN32_LEAN_AND_MEAN every target
    # here is built with.
    if(MSVC AND TARGET juce_lv2_helper)
        target_compile_options(juce_lv2_helper PRIVATE /FIwindows.h /FIshellapi.h)
    endif()
    # The bundle is the directory the plugin's module is built into.
    set(BLOKKILY_TEST_LV2_BUNDLE "$<TARGET_FILE_DIR:blokkily_test_lv2_LV2>")

    add_executable(blokkily_lv2_tests tests/lv2_tests.cpp
        ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES} ${BLOKKILY_SAMPLER_APP_SOURCES})
    target_include_directories(blokkily_lv2_tests PRIVATE src/app)
    target_link_libraries(blokkily_lv2_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_lv2_tests PRIVATE
        BLOKKILY_TEST_LV2_BUNDLE="${BLOKKILY_TEST_LV2_BUNDLE}")
    add_dependencies(blokkily_lv2_tests blokkily_test_lv2_LV2)
    foreach(case scan process parameters_and_state factory_and_engine)
        add_test(NAME lv2_${case} COMMAND blokkily_lv2_tests ${case})
        set_tests_properties(lv2_${case} PROPERTIES
            LABELS "integration;plugins;lv2;audio"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 120)
    endforeach()
endif()

if(BLOKKILY_BUILD_GUI AND BLOKKILY_BUILD_TESTS)
    target_sources(blokkily PRIVATE src/app/verify/scenario_lv2.cpp)
    add_dependencies(blokkily blokkily_test_lv2_LV2)
    add_test(NAME bdd_lv2 COMMAND blokkily --verify --scenario lv2
        --lv2-fixture ${BLOKKILY_TEST_LV2_BUNDLE}
        --project ${CMAKE_BINARY_DIR}/artifacts/lv2/lv2.blok
        --screenshot ${CMAKE_BINARY_DIR}/artifacts/lv2.png)
    set_tests_properties(bdd_lv2 PROPERTIES
        LABELS "bdd;e2e;integration;screenshot;plugins;lv2;project"
        ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}" TIMEOUT 120)
endif()
