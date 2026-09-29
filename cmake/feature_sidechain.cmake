# Feature fragment: sidechain (phase 2, wave 5.2). See
# docs/plans/phase2-program.md and features/sidechain.feature.
#
# An insert's sidechain key (EffectSlot::sidechain) is saved and validated
# with the song and compiled with the arrangement, together with the track
# render order that puts a key's source first; that code is listed by
# feature_dynamic_modulation.cmake. The built-in compressor listens to the
# key. Multi-output plugin routing is not implemented (see the plan).

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_sidechain_tests tests/sidechain_tests.cpp)
    target_include_directories(blokkily_sidechain_tests PRIVATE tests src)
    target_link_libraries(blokkily_sidechain_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_sidechain_tests PRIVATE
        BLOKKILY_TEST_ARTIFACTS="${CMAKE_BINARY_DIR}/artifacts")
    target_compile_options(blokkily_sidechain_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)

    foreach(sc_case
            key_pre_fader
            key_aligned
            bounce_includes_sidechain)
        add_test(NAME sidechain_${sc_case}
            COMMAND blokkily_sidechain_tests ${sc_case})
        set_tests_properties(sidechain_${sc_case} PROPERTIES
            LABELS "bdd;unit;integration;audio;sidechain;mixer"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 60)
    endforeach()
    set_property(TEST sidechain_bounce_includes_sidechain APPEND PROPERTY LABELS "export")

    blokkily_add_realtime_case(sidechain LABELS "audio;sidechain;mixer")
endif()

# Plugin sidechain inputs and multi-output instruments through the real hosts
# (features/sidechain_and_multiout.feature): the suite-built CLAP and VST3
# fixtures, created through the production processor factory.
if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_plugin_routing_tests
        tests/plugin_routing_tests.cpp
        ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES}
        ${BLOKKILY_SAMPLER_APP_SOURCES})
    target_include_directories(blokkily_plugin_routing_tests PRIVATE src src/app tests)
    target_link_libraries(blokkily_plugin_routing_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_plugin_routing_tests PRIVATE
        ${BLOKKILY_EFFECT_FIXTURE_DEFINITIONS}
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_VST3_PATH="$<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../.."
        BLOKKILY_TEST_ARTIFACTS="${CMAKE_BINARY_DIR}/artifacts")
    target_compile_options(blokkily_plugin_routing_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_plugin_routing_tests
        blokkily_test_clap blokkily_test_vst3_VST3
        blokkily_test_clap_effect blokkily_test_vst3_effect_VST3)
    foreach(routing_case
            ports_declared
            clap_key
            vst3_key
            plugin_key_bounce)
        add_test(NAME sidechain_${routing_case}
            COMMAND blokkily_plugin_routing_tests ${routing_case})
        set_tests_properties(sidechain_${routing_case} PROPERTIES
            LABELS "bdd;integration;audio;sidechain;mixer;plugins;clap;vst3"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 120)
    endforeach()
    set_property(TEST sidechain_plugin_key_bounce APPEND PROPERTY LABELS "export")
    foreach(routing_case
            clap_multi_out
            vst3_multi_out
            multi_out_order
            multi_out_bounce
            multi_out_song_model)
        add_test(NAME multiout_${routing_case}
            COMMAND blokkily_plugin_routing_tests ${routing_case})
        set_tests_properties(multiout_${routing_case} PROPERTIES
            LABELS "bdd;integration;audio;multiout;mixer;plugins;clap;vst3"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 120)
    endforeach()
    set_property(TEST multiout_multi_out_bounce APPEND PROPERTY LABELS "export")
    set_property(TEST multiout_multi_out_song_model APPEND PROPERTY LABELS "project;schema")

    # The CLAP/VST3 fixture paths are defined for the real-time binary by the
    # plugin boundary and built-in effects fragments.
    blokkily_add_realtime_case(plugin_routing LABELS "audio;sidechain;multiout;mixer;clap;vst3;plugins")
endif()

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE src/app/verify/scenario_routing.cpp)
    if(BLOKKILY_BUILD_TESTS)
        # Plugin sidechains and multi-output instruments in the real
        # application: the CLAP effect keyed from the rendered rack, the CLAP
        # synth's aux output broken out with the strip's + OUT, heard through
        # the production callback, saved, loaded and exported.
        add_dependencies(blokkily blokkily_test_clap_effect)
        add_test(NAME bdd_plugin_routing
            COMMAND blokkily --verify --scenario routing
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --clap-effect-fixture $<TARGET_FILE:blokkily_test_clap_effect>
                --vst3-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..
                --soundfont-fixture ${BLOKKILY_TEST_SF2}
                --project ${CMAKE_BINARY_DIR}/artifacts/plugin-routing.blok
                --export ${CMAKE_BINARY_DIR}/artifacts/plugin-routing.wav
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/plugin-routing.png)
        set_tests_properties(bdd_plugin_routing PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;clap;sidechain;multiout;mixer;export"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 300)
    endif()
endif()
