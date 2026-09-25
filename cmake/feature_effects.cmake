# Feature fragment: effects (item 2.4, effects). See docs/plans/daw-program.md.
#
# Insert chains on tracks, returns and the master bus; sends; plugin delay
# compensation live and in the bounce; the browser's instrument/effect kinds
# with the built-in effects listed; effect state saved on rebuild and on save.
# The engine stages live in src/audio/engine/engine_effects.cpp (listed by the
# engine seams fragment); the application side and the panels are added here.
# features/effects.feature maps its scenarios to the tests below.

set(BLOKKILY_EFFECTS_QML_FILES
    ui/EffectRack.qml
    ui/SendDials.qml
    ui/ReturnStrip.qml)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        src/app/app_controller_effects.cpp
        src/app/song_model_effects.cpp
        src/app/verify/scenario_effects.cpp)
    # Served beside Main.qml like every other panel, so they find one another
    # and the Theme singleton without importing their own module.
    foreach(qml_file IN LISTS BLOKKILY_EFFECTS_QML_FILES)
        get_filename_component(qml_name "${qml_file}" NAME)
        set_source_files_properties("${qml_file}" PROPERTIES QT_RESOURCE_ALIAS "${qml_name}")
    endforeach()
    qt_target_qml_sources(blokkily QML_FILES ${BLOKKILY_EFFECTS_QML_FILES})
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_effects_tests
        tests/effects_tests.cpp
        ${BLOKKILY_ENGINE_SEAMS_APP_SOURCES})
    target_include_directories(blokkily_effects_tests PRIVATE src src/app tests)
    target_link_libraries(blokkily_effects_tests PRIVATE blokkily_core)
    target_compile_definitions(blokkily_effects_tests PRIVATE
        ${BLOKKILY_EFFECT_FIXTURE_DEFINITIONS}
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_VST3_PATH="$<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../.."
        BLOKKILY_TEST_ARTIFACTS="${CMAKE_BINARY_DIR}/artifacts")
    target_compile_options(blokkily_effects_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    add_dependencies(blokkily_effects_tests
        blokkily_test_clap blokkily_test_vst3_VST3
        blokkily_test_clap_effect blokkily_test_vst3_effect_VST3)
    foreach(effects_case
            insert_chain
            bypass_live
            sends_and_returns
            solo_safe_returns
            delay_compensation
            master_insert
            project_records
            bounce_trims_latency
            tempo_synced_delay
            transport_per_chunk
            graph_signature
            insert_adoption
            state_capture
            scan_kind
            builtin_catalog)
        add_test(NAME effects_${effects_case} COMMAND blokkily_effects_tests ${effects_case})
        set_tests_properties(effects_${effects_case} PROPERTIES
            LABELS "unit;integration;audio;effects"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}" TIMEOUT 300)
    endforeach()
    set_property(TEST effects_insert_chain effects_bypass_live effects_delay_compensation
                      effects_master_insert effects_bounce_trims_latency effects_scan_kind
                 APPEND PROPERTY LABELS "plugins;clap;vst3")
    set_property(TEST effects_sends_and_returns effects_solo_safe_returns
                 APPEND PROPERTY LABELS "mixer")
    set_property(TEST effects_project_records effects_bounce_trims_latency
                 APPEND PROPERTY LABELS "project;export")

    # The insert chains, sends, returns and compensation keep the real-time
    # rules through seeks, wraps and live mixer moves.
    blokkily_add_realtime_case(effects LABELS "audio;effects;clap;vst3;plugins;mixer")

    if(BLOKKILY_BUILD_GUI)
        # The effects in the real application: the browser's kinds, inserting
        # and bypassing through the rendered rack, a return fed by a send,
        # effect state kept across a rebuild and a save, and the bounce.
        add_test(NAME bdd_effects
            COMMAND blokkily --verify --scenario effects
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --vst3-fixture $<TARGET_FILE_DIR:blokkily_test_vst3_VST3>/../..
                --soundfont-fixture ${BLOKKILY_TEST_SF2}
                --clap-effect-fixture $<TARGET_FILE:blokkily_test_clap_effect>
                --project ${CMAKE_BINARY_DIR}/artifacts/effects.blok
                --export ${CMAKE_BINARY_DIR}/artifacts/effects.wav
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/effects.png)
        set_tests_properties(bdd_effects PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;clap;effects;mixer;export"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 300)
    endif()
endif()
