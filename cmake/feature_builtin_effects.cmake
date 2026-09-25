# Feature fragment: builtin_effects (item 1.8, built-in effect DSP). See docs/plans/daw-program.md.
#
# The effects Blokkily provides itself (EQ, delay, reverb, compressor) behind
# the PluginInstance boundary, and the two effect fixtures the effects work
# (2.4) will prove its insert chain with: a real .clap and a real .vst3 bundle,
# each doing something that can be read off the rendered audio. No engine
# insert chain and no UI yet; features/builtin_effects.feature maps its
# scenarios to the tests below.

target_sources(blokkily_core PRIVATE
    src/effects/builtin.cpp
    src/effects/builtin_effect.cpp
    src/effects/builtin_effect.hpp
    src/effects/eq3.cpp
    src/effects/delay.cpp
    src/effects/reverb.cpp
    src/effects/compressor.cpp)

if(BLOKKILY_BUILD_TESTS)
    # The CLAP effect fixture: input delayed 64 samples (its reported latency)
    # times Gain. Its own directory, so instrument scans never meet it.
    add_library(blokkily_test_clap_effect MODULE tests/fixtures/test_clap_effect.cpp)
    target_include_directories(blokkily_test_clap_effect PRIVATE third_party/clap/include)
    set_target_properties(blokkily_test_clap_effect PROPERTIES
        PREFIX "" SUFFIX ".clap" OUTPUT_NAME "blokkily-test-effect"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/clap-effect-fixture")

    # The VST3 effect fixture: input delayed 32 samples (its reported
    # latency), inverted and times Gain.
    juce_add_plugin(blokkily_test_vst3_effect
        COMPANY_NAME Blokkily
        PLUGIN_MANUFACTURER_CODE Blok
        PLUGIN_CODE Bfx1
        FORMATS VST3
        PRODUCT_NAME "Blokkily Test VST3 Effect"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        VST3_CATEGORIES Fx)
    target_sources(blokkily_test_vst3_effect PRIVATE tests/fixtures/test_vst3_effect.cpp)
    target_compile_definitions(blokkily_test_vst3_effect PRIVATE JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(blokkily_test_vst3_effect PRIVATE
        juce::juce_audio_utils juce::juce_recommended_config_flags)

    set(BLOKKILY_EFFECT_FIXTURE_DEFINITIONS
        BLOKKILY_TEST_CLAP_EFFECT_PATH="$<TARGET_FILE:blokkily_test_clap_effect>"
        BLOKKILY_TEST_VST3_EFFECT_PATH="$<TARGET_FILE_DIR:blokkily_test_vst3_effect_VST3>/../..")

    add_executable(blokkily_builtin_effects_tests tests/builtin_effects_tests.cpp)
    target_include_directories(blokkily_builtin_effects_tests PRIVATE tests)
    target_link_libraries(blokkily_builtin_effects_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_builtin_effects_tests PRIVATE
        ${BLOKKILY_EFFECT_FIXTURE_DEFINITIONS})
    target_compile_options(blokkily_builtin_effects_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_builtin_effects_tests
        blokkily_test_clap_effect blokkily_test_vst3_effect_VST3)
    foreach(effects_case
            eq3_response
            delay_echoes
            delay_tempo_sync
            reverb_tail
            compressor_level
            determinism
            interface
            clap_effect
            vst3_effect
            fixture_chain)
        add_test(NAME builtin_${effects_case}
            COMMAND blokkily_builtin_effects_tests ${effects_case})
        set_tests_properties(builtin_${effects_case} PROPERTIES
            LABELS "unit;audio;effects"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    set_property(TEST builtin_interface APPEND PROPERTY LABELS "parameters;project")
    set_property(TEST builtin_clap_effect APPEND PROPERTY LABELS "integration;plugins;clap")
    set_property(TEST builtin_vst3_effect APPEND PROPERTY LABELS "integration;plugins;vst3")
    set_property(TEST builtin_fixture_chain APPEND PROPERTY LABELS "integration;plugins;clap;vst3")

    # The built-ins and both effect fixtures process without allocating.
    target_compile_definitions(blokkily_realtime_checks PRIVATE
        ${BLOKKILY_EFFECT_FIXTURE_DEFINITIONS})
    add_dependencies(blokkily_realtime_checks
        blokkily_test_clap_effect blokkily_test_vst3_effect_VST3)
    blokkily_add_realtime_case(builtin_effects LABELS "audio;effects;clap;vst3;plugins")
endif()
