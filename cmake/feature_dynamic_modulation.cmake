# Feature fragment: dynamic_modulation (phase 2, wave 5.1). See
# docs/plans/phase2-program.md and features/dynamic_modulation.feature.
#
# LFOs, macros and envelope followers are part of the song (Song::modulators,
# records_modulation.cpp, schema in song_schema.cpp). The engine compiles them
# with the arrangement and takes live moves as atomics
# (src/audio/engine/engine_modulation.cpp); the modulation panel edits them.
# The same engine file and record module carry the sidechain keys of wave 5.2
# (feature_sidechain.cmake).

target_sources(blokkily_core PRIVATE
    src/audio/modulation.cpp
    src/audio/engine/engine_modulation.cpp
    src/project/records_modulation.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        src/app/song_model_modulation.cpp
        src/app/app_controller_modulation.cpp
        src/app/verify/scenario_modulation.cpp)
    set_source_files_properties(ui/ModulationPanel.qml PROPERTIES
        QT_RESOURCE_ALIAS "ModulationPanel.qml")
    qt_target_qml_sources(blokkily QML_FILES ui/ModulationPanel.qml)
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_dynamic_modulation_tests tests/dynamic_modulation_tests.cpp)
    target_include_directories(blokkily_dynamic_modulation_tests PRIVATE tests)
    target_link_libraries(blokkily_dynamic_modulation_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_dynamic_modulation_tests PRIVATE
        ${BLOKKILY_EFFECT_FIXTURE_DEFINITIONS}
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_ARTIFACTS="${CMAKE_BINARY_DIR}/artifacts")
    target_compile_options(blokkily_dynamic_modulation_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_dynamic_modulation_tests blokkily_test_clap blokkily_test_clap_effect)

    foreach(mod_case
            lfo_shapes
            envelope_follower
            lfo_reaches_clap
            macro_live
            follower_hears_source
            bounce_matches_playback
            removed_modulation_releases
            song_model)
        add_test(NAME dynamic_modulation_${mod_case}
            COMMAND blokkily_dynamic_modulation_tests ${mod_case})
        set_tests_properties(dynamic_modulation_${mod_case} PROPERTIES
            LABELS "bdd;unit;integration;audio;modulation"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 120)
    endforeach()
    set_property(TEST dynamic_modulation_lfo_reaches_clap dynamic_modulation_macro_live
                      dynamic_modulation_follower_hears_source
                      dynamic_modulation_bounce_matches_playback
                      dynamic_modulation_removed_modulation_releases
                 APPEND PROPERTY LABELS "clap;plugins")
    set_property(TEST dynamic_modulation_bounce_matches_playback APPEND PROPERTY LABELS "export")
    set_property(TEST dynamic_modulation_song_model APPEND PROPERTY LABELS "project;schema")

    blokkily_add_realtime_case(dynamic_modulation LABELS "audio;modulation;clap")

    if(BLOKKILY_BUILD_GUI)
        # Modulation and sidechain in the real application: keying a
        # compressor from another track in the rendered rack, adding an LFO
        # and a macro in the modulation panel and aiming them at the CLAP
        # instrument, all heard through the production callback and saved.
        add_test(NAME bdd_modulation
            COMMAND blokkily --verify --scenario modulation
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --vst3-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..
                --soundfont-fixture ${BLOKKILY_TEST_SF2}
                --project ${CMAKE_BINARY_DIR}/artifacts/modulation.blok
                --export ${CMAKE_BINARY_DIR}/artifacts/modulation.wav
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/modulation.png)
        set_tests_properties(bdd_modulation PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;clap;modulation;sidechain;mixer;export"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 300)
    endif()
endif()
