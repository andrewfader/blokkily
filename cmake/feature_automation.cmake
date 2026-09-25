# Feature fragment: automation (item 3.1, record stage B). See docs/plans/daw-program.md.
#
# features/automation.feature. Strip lanes (gain, pan, mute) are compiled
# into an envelope sampled every 256 samples; parameter lanes of instruments
# and inserts on any bus become timeline events chased on every seek. A
# track's automation mode (off, read, touch, latch, write) decides whether its
# lanes play and whether moves of its strip and of its plugins' own knobs are
# recorded into them. The engine stage lives in
# src/audio/engine/engine_automation.cpp (listed by the engine seams
# fragment); the recorder, the application side and the lane editor are here.

target_sources(blokkily_core PRIVATE src/sequencer/automation_take.cpp)

if(BLOKKILY_BUILD_GUI)
    target_sources(blokkily PRIVATE
        src/app/app_controller_automation.cpp
        src/app/song_model_automation.cpp
        src/app/verify/scenario_automation.cpp)
    set_source_files_properties(ui/AutomationLane.qml PROPERTIES
        QT_RESOURCE_ALIAS "AutomationLane.qml")
    qt_target_qml_sources(blokkily QML_FILES ui/AutomationLane.qml)
endif()

if(BLOKKILY_BUILD_TESTS)
    add_executable(blokkily_automation_tests tests/automation_tests.cpp)
    target_include_directories(blokkily_automation_tests PRIVATE src tests)
    target_link_libraries(blokkily_automation_tests PRIVATE blokkily_core)
    target_compile_options(blokkily_automation_tests PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Werror>)
    target_compile_definitions(blokkily_automation_tests PRIVATE
        ${BLOKKILY_EFFECT_FIXTURE_DEFINITIONS}
        BLOKKILY_TEST_CLAP_PATH="$<TARGET_FILE:blokkily_test_clap>"
        BLOKKILY_TEST_ARTIFACTS="${CMAKE_BINARY_DIR}/artifacts")
    add_dependencies(blokkily_automation_tests blokkily_test_clap blokkily_test_clap_effect)
    foreach(automation_case
            gain_ramp_bounce
            pan_and_mute_lanes
            touch_override
            strip_moves_replay
            plugin_moves_lane
            chase_on_seek
            effect_lane_on_return
            round_trip
            solo_with_lane
            post_fader_send
            recorder_passes)
        add_test(NAME automation_${automation_case}
            COMMAND blokkily_automation_tests ${automation_case})
        set_tests_properties(automation_${automation_case} PROPERTIES
            LABELS "bdd;unit;integration;audio;automation"
            ENVIRONMENT "${BLOKKILY_HEADLESS_TEST_ENV}"
            TIMEOUT 120)
    endforeach()
    set_property(TEST automation_gain_ramp_bounce automation_plugin_moves_lane
                 APPEND PROPERTY LABELS "export;clap")
    set_property(TEST automation_pan_and_mute_lanes automation_touch_override
                      automation_strip_moves_replay automation_solo_with_lane
                      automation_post_fader_send
                 APPEND PROPERTY LABELS "mixer;clap")
    set_property(TEST automation_chase_on_seek automation_effect_lane_on_return
                 APPEND PROPERTY LABELS "clap;effects;parameters")
    set_property(TEST automation_round_trip APPEND PROPERTY LABELS "project")

    # Strip envelopes, parameter lanes on an instrument and on inserts, and
    # strip moves with touch bits, through seeks, wraps and recompiles,
    # without the render callback allocating.
    blokkily_add_realtime_case(automation LABELS "audio;automation;clap;mixer;effects")

    if(BLOKKILY_BUILD_GUI)
        # Part 2 of "record everything": the mode chip on a strip, a fader
        # dragged while the song plays in touch mode becoming a lane whose
        # points the lane editor draws and edits, the lane heard in the
        # pump, one undo for the take, and a save and a load.
        add_test(NAME bdd_automation
            COMMAND blokkily --verify --scenario automation
                --clap-fixture $<TARGET_FILE:blokkily_test_clap>
                --project ${CMAKE_BINARY_DIR}/artifacts/automation.blok
                --screenshot ${CMAKE_BINARY_DIR}/artifacts/automation.png)
        set_tests_properties(bdd_automation PROPERTIES
            LABELS "bdd;e2e;integration;screenshot;automation;recording;clap;project;mixer"
            ENVIRONMENT "${BLOKKILY_OFFSCREEN_GATE_ENV}"
            TIMEOUT 180)
    endif()
endif()
